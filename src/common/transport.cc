#include "nds/tcp_bootstrap.hh"

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

namespace nds::transport {
namespace {

CodecResult validate_qp_info(const QpInfo *info) {
    if (info == nullptr) {
        return CodecResult::InvalidRecord;
    }
    if (info->qp_num == 0U || info->qp_num > UINT32_C(0x00ffffff)) {
        return CodecResult::InvalidRecord;
    }
    if (info->psn > UINT32_C(0x00ffffff)) {
        return CodecResult::InvalidRecord;
    }
    if (info->port_num == 0U) {
        return CodecResult::InvalidRecord;
    }
    if (info->path_mtu == 0U) {
        return CodecResult::InvalidRecord;
    }
    if (info->traffic_class > UINT8_MAX) {
        return CodecResult::InvalidRecord;
    }
    if (info->service_level > 15U) {
        return CodecResult::InvalidRecord;
    }
    if (info->retry_count > 7U || info->retry_timeout > 31U) {
        return CodecResult::InvalidRecord;
    }
    return CodecResult::Ok;
}

uint64_t host_to_network_u64(uint64_t value) {
    return (static_cast<uint64_t>(htonl(static_cast<uint32_t>(value))) << 32U) |
           htonl(static_cast<uint32_t>(value >> 32U));
}

uint64_t network_to_host_u64(uint64_t value) {
    return (static_cast<uint64_t>(ntohl(static_cast<uint32_t>(value))) << 32U) |
           ntohl(static_cast<uint32_t>(value >> 32U));
}

CodecResult validate_remote_memory(const RemoteMemory *memory) {
    if (memory == nullptr || memory->address == 0U || memory->length == 0U || memory->remote_key == 0U)
        return CodecResult::InvalidRecord;
    return CodecResult::Ok;
}

}  // namespace

CodecResult encode(const QpInfo *info, wire::QpInfo *encoded) {
    if (encoded == nullptr) {
        return CodecResult::InvalidArgument;
    }
    if (validate_qp_info(info) != CodecResult::Ok) {
        return CodecResult::InvalidRecord;
    }

    *encoded = {};
    encoded->magic = htonl(wire::kQpInfoMagic);
    encoded->version = htons(wire::kQpInfoVersion);
    encoded->qp_num = htonl(info->qp_num);
    encoded->psn = htonl(info->psn);
    encoded->port_num = htons(info->port_num);
    encoded->gid_index = htons(info->gid_index);
    encoded->path_mtu = htonl(info->path_mtu);
    encoded->traffic_class = htonl(info->traffic_class);
    encoded->service_level = htonl(info->service_level);
    encoded->retry_count = htonl(info->retry_count);
    encoded->retry_timeout = htonl(info->retry_timeout);
    memcpy(encoded->gid, info->gid, sizeof(encoded->gid));
    return CodecResult::Ok;
}

CodecResult encode(const RemoteMemory *memory, wire::RemoteMemory *encoded) {
    if (encoded == nullptr)
        return CodecResult::InvalidArgument;
    if (validate_remote_memory(memory) != CodecResult::Ok)
        return CodecResult::InvalidRecord;
    *encoded = {};
    encoded->magic = htonl(wire::kRemoteMemoryMagic);
    encoded->version = htons(wire::kRemoteMemoryVersion);
    encoded->address = host_to_network_u64(memory->address);
    encoded->length = htonl(memory->length);
    encoded->remote_key = htonl(memory->remote_key);
    return CodecResult::Ok;
}

int mtu_is_supported(uint32_t mtu_bytes) {
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

uint32_t select_mtu(uint32_t local_active_mtu, uint32_t peer_reported_mtu) {
    (void)peer_reported_mtu;
    return mtu_is_supported(local_active_mtu) ? local_active_mtu : 0U;
}

}  // namespace nds::transport

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

Result<nds::transport::QpInfo> TcpPeerExchange::exchange(int fd, const nds::transport::QpInfo &local,
                                                         bool client_order) {
    nds::wire::QpInfo local_wire{};
    nds::wire::QpInfo peer_wire{};
    if (fd < 0) {
        return unexpected(ErrorCode::kTransport, "TCP bootstrap socket is not open");
    }
    if (nds::transport::encode(&local, &local_wire) != nds::transport::CodecResult::Ok) {
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
    nds::transport::QpInfo peer{};
    if (nds::transport::decode(&peer_wire, &peer) != nds::transport::CodecResult::Ok) {
        return unexpected(ErrorCode::kTransport, "cannot decode peer QP info");
    }
    return peer;
}

Result<nds::transport::QpInfo> TcpPeerExchange::exchange_as_client(const nds::transport::QpInfo &local) const {
    return exchange(fd_, local, true);
}

Result<nds::transport::QpInfo> TcpPeerExchange::exchange_as_server(const nds::transport::QpInfo &local) const {
    return exchange(fd_, local, false);
}

Result<std::uint32_t> TcpPeerExchange::negotiate_qp_count_as_client(std::uint32_t requested) const {
    if (fd_ < 0 || requested == 0U || requested > wire::kMaxQpInfoBatch)
        return unexpected(ErrorCode::kInvalidArgument, "invalid requested QP count");
    wire::QpCount request{};
    request.magic = htonl(wire::kQpCountMagic);
    request.version = htons(wire::kQpCountVersion);
    request.count = htonl(requested);
    if (const auto sent = write_full(fd_, &request, sizeof(request)); !sent)
        return unexpected(sent.error());

    wire::QpCount response{};
    if (const auto received = read_full(fd_, &response, sizeof(response)); !received)
        return unexpected(received.error());
    const std::uint32_t accepted = ntohl(response.count);
    if (ntohl(response.magic) != wire::kQpCountMagic || ntohs(response.version) != wire::kQpCountVersion ||
        accepted == 0U || accepted > requested || accepted > wire::kMaxQpInfoBatch) {
        return unexpected(ErrorCode::kTransport, "invalid negotiated QP count");
    }
    return accepted;
}

Result<std::uint32_t> TcpPeerExchange::negotiate_qp_count_as_server(std::uint32_t maximum) const {
    if (fd_ < 0 || maximum == 0U || maximum > wire::kMaxQpInfoBatch)
        return unexpected(ErrorCode::kInvalidArgument, "invalid maximum QP count");
    wire::QpCount request{};
    if (const auto received = read_full(fd_, &request, sizeof(request)); !received)
        return unexpected(received.error());
    const std::uint32_t requested = ntohl(request.count);
    if (ntohl(request.magic) != wire::kQpCountMagic || ntohs(request.version) != wire::kQpCountVersion ||
        requested == 0U || requested > wire::kMaxQpInfoBatch) {
        return unexpected(ErrorCode::kTransport, "invalid requested QP count");
    }
    const std::uint32_t accepted = requested < maximum ? requested : maximum;
    wire::QpCount response{};
    response.magic = htonl(wire::kQpCountMagic);
    response.version = htons(wire::kQpCountVersion);
    response.count = htonl(accepted);
    if (const auto sent = write_full(fd_, &response, sizeof(response)); !sent)
        return unexpected(sent.error());
    return accepted;
}

Result<std::vector<nds::transport::QpInfo>> TcpPeerExchange::exchange_many(
    int fd, const std::vector<nds::transport::QpInfo> &local, bool client_order) {
    if (fd < 0 || local.empty() || local.size() > wire::kMaxQpInfoBatch)
        return unexpected(ErrorCode::kInvalidArgument, "QP batch exchange requires a bounded nonempty batch");

    std::vector<wire::QpInfo> local_wire(local.size());
    for (std::size_t index = 0U; index < local.size(); ++index) {
        if (nds::transport::encode(&local[index], &local_wire[index]) != nds::transport::CodecResult::Ok)
            return unexpected(ErrorCode::kTransport, "cannot encode local QP batch");
    }
    wire::QpInfoBatchHeader local_header{};
    local_header.magic = htonl(wire::kQpInfoBatchMagic);
    local_header.version = htons(wire::kQpInfoVersion);
    local_header.count = htonl(static_cast<std::uint32_t>(local_wire.size()));

    const auto receive_peer = [&]() -> Result<std::vector<nds::transport::QpInfo>> {
        wire::QpInfoBatchHeader peer_header{};
        if (const auto received = read_full(fd, &peer_header, sizeof(peer_header)); !received)
            return unexpected(received.error());
        const std::uint32_t count = ntohl(peer_header.count);
        if (ntohl(peer_header.magic) != wire::kQpInfoBatchMagic || ntohs(peer_header.version) != wire::kQpInfoVersion ||
            count == 0U || count > wire::kMaxQpInfoBatch || count != local_wire.size()) {
            return unexpected(ErrorCode::kTransport, "invalid or mismatched QP batch header");
        }
        std::vector<wire::QpInfo> peer_wire(count);
        if (const auto received = read_full(fd, peer_wire.data(), peer_wire.size() * sizeof(wire::QpInfo)); !received)
            return unexpected(received.error());
        std::vector<nds::transport::QpInfo> peer(count);
        for (std::size_t index = 0U; index < peer.size(); ++index) {
            if (nds::transport::decode(&peer_wire[index], &peer[index]) != nds::transport::CodecResult::Ok)
                return unexpected(ErrorCode::kTransport, "invalid QP record in batch");
        }
        return peer;
    };
    const auto send_local = [&]() -> Result<void> {
        NDS_RETURN_IF_ERROR(write_full(fd, &local_header, sizeof(local_header)));
        return write_full(fd, local_wire.data(), local_wire.size() * sizeof(wire::QpInfo));
    };
    if (client_order) {
        NDS_RETURN_IF_ERROR(send_local());
        return receive_peer();
    }
    const auto peer = receive_peer();
    if (!peer)
        return unexpected(peer.error());
    NDS_RETURN_IF_ERROR(send_local());
    return peer;
}

Result<std::vector<nds::transport::QpInfo>> TcpPeerExchange::exchange_as_client(
    const std::vector<nds::transport::QpInfo> &local) const {
    return exchange_many(fd_, local, true);
}

Result<std::vector<nds::transport::QpInfo>> TcpPeerExchange::exchange_as_server(
    const std::vector<nds::transport::QpInfo> &local) const {
    return exchange_many(fd_, local, false);
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

namespace nds::transport {

CodecResult decode(const wire::QpInfo *encoded, QpInfo *info) {
    QpInfo decoded;

    if (encoded == nullptr || info == nullptr) {
        return CodecResult::InvalidArgument;
    }
    if (ntohl(encoded->magic) != wire::kQpInfoMagic) {
        return CodecResult::InvalidRecord;
    }
    if (ntohs(encoded->version) != wire::kQpInfoVersion) {
        return CodecResult::InvalidRecord;
    }

    decoded = {};
    decoded.qp_num = ntohl(encoded->qp_num);
    decoded.psn = ntohl(encoded->psn);
    decoded.port_num = ntohs(encoded->port_num);
    decoded.gid_index = ntohs(encoded->gid_index);
    decoded.path_mtu = ntohl(encoded->path_mtu);
    decoded.traffic_class = ntohl(encoded->traffic_class);
    decoded.service_level = ntohl(encoded->service_level);
    decoded.retry_count = ntohl(encoded->retry_count);
    decoded.retry_timeout = ntohl(encoded->retry_timeout);
    memcpy(decoded.gid, encoded->gid, sizeof(decoded.gid));
    if (validate_qp_info(&decoded) != CodecResult::Ok) {
        return CodecResult::InvalidRecord;
    }
    *info = decoded;
    return CodecResult::Ok;
}

CodecResult decode(const wire::RemoteMemory *encoded, RemoteMemory *memory) {
    if (encoded == nullptr || memory == nullptr)
        return CodecResult::InvalidArgument;
    if (ntohl(encoded->magic) != wire::kRemoteMemoryMagic || ntohs(encoded->version) != wire::kRemoteMemoryVersion)
        return CodecResult::InvalidRecord;
    const RemoteMemory decoded{network_to_host_u64(encoded->address), ntohl(encoded->length),
                               ntohl(encoded->remote_key)};
    if (validate_remote_memory(&decoded) != CodecResult::Ok)
        return CodecResult::InvalidRecord;
    *memory = decoded;
    return CodecResult::Ok;
}

}  // namespace nds::transport
