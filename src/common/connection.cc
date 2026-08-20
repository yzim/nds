#include "nds/connection.hh"

#include <arpa/inet.h>
#include <cerrno>
#include <charconv>
#include <cstring>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <utility>

static enum nds_qp_info_result nds_qp_info_validate(const nds_qp_info *info) {
    if (info == nullptr) {
        return NDS_QP_INFO_RESULT_INVALID_RECORD;
    }
    if (info->qp_num == 0U || info->qp_num > UINT32_C(0x00ffffff)) {
        return NDS_QP_INFO_RESULT_INVALID_RECORD;
    }
    if (info->psn > UINT32_C(0x00ffffff)) {
        return NDS_QP_INFO_RESULT_INVALID_RECORD;
    }
    if (info->port_num == 0U) {
        return NDS_QP_INFO_RESULT_INVALID_RECORD;
    }
    if (info->path_mtu == 0U) {
        return NDS_QP_INFO_RESULT_INVALID_RECORD;
    }
    if (info->traffic_class > UINT8_MAX) {
        return NDS_QP_INFO_RESULT_INVALID_RECORD;
    }
    if (info->service_level > 15U) {
        return NDS_QP_INFO_RESULT_INVALID_RECORD;
    }
    if (info->retry_count > 7U || info->retry_timeout > 31U) {
        return NDS_QP_INFO_RESULT_INVALID_RECORD;
    }
    return NDS_QP_INFO_RESULT_OK;
}

enum nds_qp_info_result nds_qp_info_encode(const nds_qp_info *info, nds_qp_info_wire *wire) {
    if (wire == nullptr) {
        return NDS_QP_INFO_RESULT_INVALID_ARGUMENT;
    }
    if (nds_qp_info_validate(info) != NDS_QP_INFO_RESULT_OK) {
        return NDS_QP_INFO_RESULT_INVALID_RECORD;
    }

    *wire = {};
    wire->magic = htonl(NDS_QP_INFO_WIRE_MAGIC);
    wire->version = htons(NDS_QP_INFO_WIRE_VERSION);
    wire->qp_num = htonl(info->qp_num);
    wire->psn = htonl(info->psn);
    wire->port_num = htons(info->port_num);
    wire->gid_index = htons(info->gid_index);
    wire->path_mtu = htonl(info->path_mtu);
    wire->traffic_class = htonl(info->traffic_class);
    wire->service_level = htonl(info->service_level);
    wire->retry_count = htonl(info->retry_count);
    wire->retry_timeout = htonl(info->retry_timeout);
    memcpy(wire->gid, info->gid, sizeof(wire->gid));
    return NDS_QP_INFO_RESULT_OK;
}

int nds_qp_mtu_is_supported(uint32_t mtu_bytes) {
    switch (mtu_bytes) {
        case 256U:
        case 512U:
        case 1024U:
        case 2048U:
        case 4096U:
            return 1;
        default:
            return 0;
    }
}

uint32_t nds_qp_mtu_select(uint32_t local_active_mtu, uint32_t peer_reported_mtu) {
    (void)peer_reported_mtu;
    return nds_qp_mtu_is_supported(local_active_mtu) ? local_active_mtu : 0U;
}

namespace nds {
namespace {

std::string system_error(const char *operation) {
    return std::string(operation) + ": " + std::strerror(errno);
}

Result<void> wait_for_fd(int fd, short events, std::uint32_t timeout_ms) {
    pollfd descriptor{};
    descriptor.fd = fd;
    descriptor.events = events;
    const int result = poll(&descriptor, 1, static_cast<int>(timeout_ms));

    if (result == 0) {
        return unexpected(ErrorCode::kTransport, "TCP bootstrap timeout");
    }
    if (result < 0) {
        return unexpected(ErrorCode::kTransport, system_error("poll"));
    }
    if ((descriptor.revents & (POLLERR | POLLHUP | POLLNVAL)) != 0) {
        return unexpected(ErrorCode::kTransport, "TCP bootstrap socket became unavailable");
    }
    return {};
}

}  // namespace

Result<TcpAddress> parse_tcp_address(const std::string &address) {
    const std::size_t separator = address.rfind(':');
    if (separator == std::string::npos || separator == 0U || separator + 1U == address.size())
        return unexpected(ErrorCode::kInvalidArgument, "TCP address must be IPv4:port");
    TcpAddress parsed{address.substr(0U, separator), 0U};
    const std::string port_text = address.substr(separator + 1U);
    const auto [last, error] = std::from_chars(port_text.data(), port_text.data() + port_text.size(), parsed.port);
    in_addr ipv4{};
    if (inet_pton(AF_INET, parsed.ipv4.c_str(), &ipv4) != 1 || error != std::errc{} ||
        last != port_text.data() + port_text.size() || parsed.port == 0U)
        return unexpected(ErrorCode::kInvalidArgument, "TCP address must be IPv4:port");
    return parsed;
}

TcpPeerExchange::TcpPeerExchange(int fd) noexcept : fd_(fd) {}

TcpPeerExchange::~TcpPeerExchange() {
    if (fd_ >= 0) {
        (void)close(fd_);
    }
}

TcpPeerExchange::TcpPeerExchange(TcpPeerExchange &&other) noexcept : fd_(std::exchange(other.fd_, -1)) {}

TcpPeerExchange &TcpPeerExchange::operator=(TcpPeerExchange &&other) noexcept {
    if (this != &other) {
        if (fd_ >= 0)
            (void)close(fd_);
        fd_ = std::exchange(other.fd_, -1);
    }
    return *this;
}

Result<void> TcpPeerExchange::send_bytes(const void *buffer, std::size_t length) const {
    if (fd_ < 0 || buffer == nullptr)
        return unexpected(ErrorCode::kInvalidArgument, "TCP bootstrap socket and buffer are required");
    return write_full(fd_, buffer, length);
}

Result<void> TcpPeerExchange::receive_bytes(void *buffer, std::size_t length) const {
    if (fd_ < 0 || buffer == nullptr)
        return unexpected(ErrorCode::kInvalidArgument, "TCP bootstrap socket and buffer are required");
    return read_full(fd_, buffer, length);
}

Result<void> TcpPeerExchange::adopt(int fd) {
    if (fd < 0 || fd_ >= 0) {
        return unexpected(ErrorCode::kInvalidArgument, "cannot adopt TCP socket");
    }
    fd_ = fd;
    return {};
}

Result<void> TcpPeerExchange::read_full(int fd, void *buffer, std::size_t length) {
    auto *cursor = static_cast<unsigned char *>(buffer);

    while (length != 0U) {
        const ssize_t result = read(fd, cursor, length);
        if (result == 0) {
            return unexpected(ErrorCode::kTransport, "peer closed TCP bootstrap connection");
        }
        if (result < 0) {
            if (errno == EINTR) {
                continue;
            }
            return unexpected(ErrorCode::kTransport, system_error("read"));
        }
        cursor += static_cast<std::size_t>(result);
        length -= static_cast<std::size_t>(result);
    }
    return {};
}

Result<void> TcpPeerExchange::write_full(int fd, const void *buffer, std::size_t length) {
    const auto *cursor = static_cast<const unsigned char *>(buffer);

    while (length != 0U) {
        const ssize_t result = write(fd, cursor, length);
        if (result < 0) {
            if (errno == EINTR) {
                continue;
            }
            return unexpected(ErrorCode::kTransport, system_error("write"));
        }
        cursor += static_cast<std::size_t>(result);
        length -= static_cast<std::size_t>(result);
    }
    return {};
}

Result<nds_qp_info> TcpPeerExchange::exchange(int fd, const nds_qp_info &local, bool client_order) {
    nds_qp_info_wire local_wire{};
    nds_qp_info_wire peer_wire{};
    if (fd < 0) {
        return unexpected(ErrorCode::kTransport, "TCP bootstrap socket is not open");
    }
    if (nds_qp_info_encode(&local, &local_wire) != 0) {
        return unexpected(ErrorCode::kTransport, "cannot encode local QP info");
    }
    if (client_order) {
        if (const auto result = write_full(fd, &local_wire, sizeof(local_wire)); !result)
            return unexpected(result.error());
        if (const auto result = read_full(fd, &peer_wire, sizeof(peer_wire)); !result)
            return unexpected(result.error());
    } else {
        if (const auto result = read_full(fd, &peer_wire, sizeof(peer_wire)); !result)
            return unexpected(result.error());
        if (const auto result = write_full(fd, &local_wire, sizeof(local_wire)); !result)
            return unexpected(result.error());
    }
    nds_qp_info peer{};
    if (nds_qp_info_decode(&peer_wire, &peer) != 0) {
        return unexpected(ErrorCode::kTransport, "cannot decode peer QP info");
    }
    return peer;
}

Result<nds_qp_info> TcpPeerExchange::exchange_as_client(const nds_qp_info &local) const {
    return exchange(fd_, local, true);
}

Result<nds_qp_info> TcpPeerExchange::exchange_as_server(const nds_qp_info &local) const {
    return exchange(fd_, local, false);
}

Result<TcpPeerExchange> TcpPeerExchange::connect(const std::string &ipv4, std::uint16_t port,
                                                  std::uint32_t timeout_ms) {
    sockaddr_in address{};
    int socket_fd;
    int flags;
    int connect_result;

    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    if (inet_pton(AF_INET, ipv4.c_str(), &address.sin_addr) != 1) {
        return unexpected(ErrorCode::kInvalidArgument, "invalid TCP peer IPv4 address: " + ipv4);
    }
    socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_fd < 0) {
        return unexpected(ErrorCode::kTransport, system_error("socket"));
    }
    flags = fcntl(socket_fd, F_GETFL, 0);
    if (flags < 0 || fcntl(socket_fd, F_SETFL, flags | O_NONBLOCK) != 0) {
        (void)close(socket_fd);
        return unexpected(ErrorCode::kTransport, system_error("fcntl"));
    }
    connect_result = ::connect(socket_fd, reinterpret_cast<const sockaddr *>(&address), sizeof(address));
    if (connect_result != 0 && errno != EINPROGRESS) {
        (void)close(socket_fd);
        return unexpected(ErrorCode::kTransport, system_error("connect"));
    }
    if (connect_result != 0) {
        const auto waited = wait_for_fd(socket_fd, POLLOUT, timeout_ms);
        if (!waited) {
            (void)close(socket_fd);
            return unexpected(waited.error());
        }
    }
    if (connect_result != 0) {
        int socket_error = 0;
        socklen_t socket_error_length = sizeof(socket_error);

        if (getsockopt(socket_fd, SOL_SOCKET, SO_ERROR, &socket_error, &socket_error_length) != 0 ||
            socket_error != 0) {
            (void)close(socket_fd);
            return unexpected(ErrorCode::kTransport, socket_error != 0
                                                         ? std::string("connect: ") + std::strerror(socket_error)
                                                         : system_error("getsockopt"));
        }
    }
    if (fcntl(socket_fd, F_SETFL, flags) != 0) {
        (void)close(socket_fd);
        return unexpected(ErrorCode::kTransport, system_error("fcntl"));
    }
    return TcpPeerExchange(socket_fd);
}

}  // namespace nds

enum nds_qp_info_result nds_qp_info_decode(const nds_qp_info_wire *wire, nds_qp_info *info) {
    nds_qp_info decoded;

    if (wire == nullptr || info == nullptr) {
        return NDS_QP_INFO_RESULT_INVALID_ARGUMENT;
    }
    if (ntohl(wire->magic) != NDS_QP_INFO_WIRE_MAGIC) {
        return NDS_QP_INFO_RESULT_INVALID_RECORD;
    }
    if (ntohs(wire->version) != NDS_QP_INFO_WIRE_VERSION) {
        return NDS_QP_INFO_RESULT_INVALID_RECORD;
    }

    decoded = {};
    decoded.qp_num = ntohl(wire->qp_num);
    decoded.psn = ntohl(wire->psn);
    decoded.port_num = ntohs(wire->port_num);
    decoded.gid_index = ntohs(wire->gid_index);
    decoded.path_mtu = ntohl(wire->path_mtu);
    decoded.traffic_class = ntohl(wire->traffic_class);
    decoded.service_level = ntohl(wire->service_level);
    decoded.retry_count = ntohl(wire->retry_count);
    decoded.retry_timeout = ntohl(wire->retry_timeout);
    memcpy(decoded.gid, wire->gid, sizeof(decoded.gid));
    if (nds_qp_info_validate(&decoded) != NDS_QP_INFO_RESULT_OK) {
        return NDS_QP_INFO_RESULT_INVALID_RECORD;
    }
    *info = decoded;
    return NDS_QP_INFO_RESULT_OK;
}
