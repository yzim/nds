#include "nds/transport.hh"

#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

static enum nds_transport_result nds_transport_endpoint_validate(const nds_transport_endpoint *endpoint) {
    if (endpoint == nullptr) {
        return NDS_TRANSPORT_RESULT_INVALID_RECORD;
    }
    if (endpoint->qp_num == 0U || endpoint->qp_num > UINT32_C(0x00ffffff)) {
        return NDS_TRANSPORT_RESULT_INVALID_RECORD;
    }
    if (endpoint->psn > UINT32_C(0x00ffffff)) {
        return NDS_TRANSPORT_RESULT_INVALID_RECORD;
    }
    if (endpoint->port_num == 0U) {
        return NDS_TRANSPORT_RESULT_INVALID_RECORD;
    }
    if (endpoint->path_mtu == 0U) {
        return NDS_TRANSPORT_RESULT_INVALID_RECORD;
    }
    if (endpoint->traffic_class > UINT8_MAX) {
        return NDS_TRANSPORT_RESULT_INVALID_RECORD;
    }
    if (endpoint->service_level > 15U) {
        return NDS_TRANSPORT_RESULT_INVALID_RECORD;
    }
    if (endpoint->retry_count > 7U || endpoint->retry_timeout > 31U) {
        return NDS_TRANSPORT_RESULT_INVALID_RECORD;
    }
    return NDS_TRANSPORT_RESULT_OK;
}

enum nds_transport_result nds_transport_endpoint_encode(const nds_transport_endpoint *endpoint,
                                                        nds_transport_endpoint_wire *wire) {
    if (wire == nullptr) {
        return NDS_TRANSPORT_RESULT_INVALID_ARGUMENT;
    }
    if (nds_transport_endpoint_validate(endpoint) != NDS_TRANSPORT_RESULT_OK) {
        return NDS_TRANSPORT_RESULT_INVALID_RECORD;
    }

    *wire = {};
    wire->magic = htonl(NDS_TRANSPORT_WIRE_MAGIC);
    wire->version = htons(NDS_TRANSPORT_WIRE_VERSION);
    wire->qp_num = htonl(endpoint->qp_num);
    wire->psn = htonl(endpoint->psn);
    wire->port_num = htons(endpoint->port_num);
    wire->gid_index = htons(endpoint->gid_index);
    wire->path_mtu = htonl(endpoint->path_mtu);
    wire->traffic_class = htonl(endpoint->traffic_class);
    wire->service_level = htonl(endpoint->service_level);
    wire->retry_count = htonl(endpoint->retry_count);
    wire->retry_timeout = htonl(endpoint->retry_timeout);
    memcpy(wire->gid, endpoint->gid, sizeof(wire->gid));
    return NDS_TRANSPORT_RESULT_OK;
}

int nds_transport_mtu_is_supported(uint32_t mtu_bytes) {
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

uint32_t nds_transport_mtu_select(uint32_t local_active_mtu, uint32_t peer_reported_mtu) {
    (void)peer_reported_mtu;
    return nds_transport_mtu_is_supported(local_active_mtu) ? local_active_mtu : 0U;
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
        return failure(ErrorCode::kTransport, "TCP bootstrap timeout");
    }
    if (result < 0) {
        return failure(ErrorCode::kTransport, system_error("poll"));
    }
    if ((descriptor.revents & (POLLERR | POLLHUP | POLLNVAL)) != 0) {
        return failure(ErrorCode::kTransport, "TCP bootstrap socket became unavailable");
    }
    return success();
}

}  // namespace

TcpPeerExchange::TcpPeerExchange(int fd) noexcept : fd_(fd) {}

TcpPeerExchange::~TcpPeerExchange() {
    if (fd_ >= 0) {
        (void)close(fd_);
    }
}

Result<void> TcpPeerExchange::send_bytes(const void *buffer, std::size_t length) const {
    if (fd_ < 0 || buffer == nullptr)
        return failure(ErrorCode::kInvalidArgument, "TCP bootstrap socket and buffer are required");
    return write_full(fd_, buffer, length);
}

Result<void> TcpPeerExchange::receive_bytes(void *buffer, std::size_t length) const {
    if (fd_ < 0 || buffer == nullptr)
        return failure(ErrorCode::kInvalidArgument, "TCP bootstrap socket and buffer are required");
    return read_full(fd_, buffer, length);
}

Result<void> TcpPeerExchange::adopt(int fd) {
    if (fd < 0 || fd_ >= 0) {
        return failure(ErrorCode::kInvalidArgument, "cannot adopt TCP socket");
    }
    fd_ = fd;
    return success();
}

Result<void> TcpPeerExchange::read_full(int fd, void *buffer, std::size_t length) {
    auto *cursor = static_cast<unsigned char *>(buffer);

    while (length != 0U) {
        const ssize_t result = read(fd, cursor, length);
        if (result == 0) {
            return failure(ErrorCode::kTransport, "peer closed TCP bootstrap connection");
        }
        if (result < 0) {
            if (errno == EINTR) {
                continue;
            }
            return failure(ErrorCode::kTransport, system_error("read"));
        }
        cursor += static_cast<std::size_t>(result);
        length -= static_cast<std::size_t>(result);
    }
    return success();
}

Result<void> TcpPeerExchange::write_full(int fd, const void *buffer, std::size_t length) {
    const auto *cursor = static_cast<const unsigned char *>(buffer);

    while (length != 0U) {
        const ssize_t result = write(fd, cursor, length);
        if (result < 0) {
            if (errno == EINTR) {
                continue;
            }
            return failure(ErrorCode::kTransport, system_error("write"));
        }
        cursor += static_cast<std::size_t>(result);
        length -= static_cast<std::size_t>(result);
    }
    return success();
}

Result<nds_transport_endpoint> TcpPeerExchange::exchange(int fd, const nds_transport_endpoint &local,
                                                         bool client_order) {
    nds_transport_endpoint_wire local_wire{};
    nds_transport_endpoint_wire peer_wire{};
    if (fd < 0) {
        return failure(ErrorCode::kTransport, "TCP bootstrap socket is not open");
    }
    if (nds_transport_endpoint_encode(&local, &local_wire) != 0) {
        return failure(ErrorCode::kTransport, "cannot encode local transport endpoint");
    }
    if (client_order) {
        if (const auto result = write_full(fd, &local_wire, sizeof(local_wire)); !result)
            return propagate(result.error());
        if (const auto result = read_full(fd, &peer_wire, sizeof(peer_wire)); !result)
            return propagate(result.error());
    } else {
        if (const auto result = read_full(fd, &peer_wire, sizeof(peer_wire)); !result)
            return propagate(result.error());
        if (const auto result = write_full(fd, &local_wire, sizeof(local_wire)); !result)
            return propagate(result.error());
    }
    nds_transport_endpoint peer{};
    if (nds_transport_endpoint_decode(&peer_wire, &peer) != 0) {
        return failure(ErrorCode::kTransport, "cannot decode peer transport endpoint");
    }
    return success(peer);
}

Result<nds_transport_endpoint> TcpPeerExchange::exchange_as_client(const nds_transport_endpoint &local) const {
    return exchange(fd_, local, true);
}

Result<nds_transport_endpoint> TcpPeerExchange::exchange_as_server(const nds_transport_endpoint &local) const {
    return exchange(fd_, local, false);
}

Result<void> TcpPeerExchange::connect(const std::string &ipv4, std::uint16_t port, std::uint32_t timeout_ms,
                                      TcpPeerExchange *connection) {
    sockaddr_in address{};
    int socket_fd;
    int flags;
    int connect_result;

    if (connection == nullptr) {
        return failure(ErrorCode::kInvalidArgument, "TCP bootstrap requires an output connection");
    }
    if (connection->fd_ >= 0) {
        return failure(ErrorCode::kInvalidArgument, "output TCP bootstrap object already owns a socket");
    }
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    if (inet_pton(AF_INET, ipv4.c_str(), &address.sin_addr) != 1) {
        return failure(ErrorCode::kInvalidArgument, "invalid TCP peer IPv4 address: " + ipv4);
    }
    socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_fd < 0) {
        return failure(ErrorCode::kTransport, system_error("socket"));
    }
    flags = fcntl(socket_fd, F_GETFL, 0);
    if (flags < 0 || fcntl(socket_fd, F_SETFL, flags | O_NONBLOCK) != 0) {
        (void)close(socket_fd);
        return failure(ErrorCode::kTransport, system_error("fcntl"));
    }
    connect_result = ::connect(socket_fd, reinterpret_cast<const sockaddr *>(&address), sizeof(address));
    if (connect_result != 0 && errno != EINPROGRESS) {
        (void)close(socket_fd);
        return failure(ErrorCode::kTransport, system_error("connect"));
    }
    if (connect_result != 0) {
        const auto waited = wait_for_fd(socket_fd, POLLOUT, timeout_ms);
        if (!waited) {
            (void)close(socket_fd);
            return propagate(waited.error());
        }
    }
    if (connect_result != 0) {
        int socket_error = 0;
        socklen_t socket_error_length = sizeof(socket_error);

        if (getsockopt(socket_fd, SOL_SOCKET, SO_ERROR, &socket_error, &socket_error_length) != 0 ||
            socket_error != 0) {
            (void)close(socket_fd);
            return failure(ErrorCode::kTransport, socket_error != 0
                                                      ? std::string("connect: ") + std::strerror(socket_error)
                                                      : system_error("getsockopt"));
        }
    }
    if (fcntl(socket_fd, F_SETFL, flags) != 0) {
        (void)close(socket_fd);
        return failure(ErrorCode::kTransport, system_error("fcntl"));
    }
    connection->fd_ = socket_fd;
    return success();
}

}  // namespace nds

enum nds_transport_result nds_transport_endpoint_decode(const nds_transport_endpoint_wire *wire,
                                                        nds_transport_endpoint *endpoint) {
    nds_transport_endpoint decoded;

    if (wire == nullptr || endpoint == nullptr) {
        return NDS_TRANSPORT_RESULT_INVALID_ARGUMENT;
    }
    if (ntohl(wire->magic) != NDS_TRANSPORT_WIRE_MAGIC) {
        return NDS_TRANSPORT_RESULT_INVALID_RECORD;
    }
    if (ntohs(wire->version) != NDS_TRANSPORT_WIRE_VERSION) {
        return NDS_TRANSPORT_RESULT_INVALID_RECORD;
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
    if (nds_transport_endpoint_validate(&decoded) != NDS_TRANSPORT_RESULT_OK) {
        return NDS_TRANSPORT_RESULT_INVALID_RECORD;
    }
    *endpoint = decoded;
    return NDS_TRANSPORT_RESULT_OK;
}
