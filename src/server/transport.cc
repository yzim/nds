#include "transport.hh"

#include "nds/wire/transport.hh"

#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <sys/socket.h>
#include <unistd.h>
#include <utility>

namespace nds::server {

namespace {

constexpr int kListenBacklog = 8;

}  // namespace

TransportListener::~TransportListener() {
    if (listener_fd_ >= 0)
        (void)close(listener_fd_);
}

Result<void> TransportListener::open(const TransportConfig &config) {
    if (listener_fd_ >= 0 || config.max_qp_count == 0U || config.max_qp_count > nds::wire::kMaxQpInfoBatch)
        return unexpected(ErrorCode::kInvalidArgument, "invalid server QP-count limit");
    const auto listen_address = parse_tcp_address(config.listen_address);
    if (!listen_address)
        return unexpected(listen_address.error());
    const int listener = socket(AF_INET, SOCK_STREAM, 0);
    int enabled = 1;
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(listen_address->port);
    if (listener < 0 || setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &enabled, sizeof(enabled)) != 0 ||
        inet_pton(AF_INET, listen_address->ipv4.c_str(), &address.sin_addr) != 1 ||
        bind(listener, reinterpret_cast<const sockaddr *>(&address), sizeof(address)) != 0 ||
        listen(listener, kListenBacklog) != 0) {
        if (listener >= 0)
            (void)close(listener);
        return unexpected(ErrorCode::kTransport, std::strerror(errno));
    }
    listener_fd_ = listener;
    config_ = config;
    return {};
}

Result<void> TransportListener::accept(Transport *transport) {
    if (listener_fd_ < 0 || transport == nullptr)
        return unexpected(ErrorCode::kInvalidArgument, "open listener and transport are required");
    const int peer_fd = ::accept(listener_fd_, nullptr, nullptr);
    if (peer_fd < 0) {
        return unexpected(ErrorCode::kTransport, std::strerror(errno));
    }
    TcpPeerExchange bootstrap{peer_fd};
    const auto qp_count = bootstrap.negotiate_qp_count_as_server(config_.max_qp_count);
    if (!qp_count)
        return unexpected(qp_count.error());
    return transport->open(std::move(bootstrap), config_, *qp_count);
}

Result<void> Transport::open(TcpPeerExchange bootstrap, const TransportConfig &config, std::uint32_t qp_count) {
    if (qp_count == 0U || qp_count > config.max_qp_count || qp_count > nds::wire::kMaxQpInfoBatch)
        return unexpected(ErrorCode::kInvalidArgument, "invalid negotiated QP count");
    if (const auto opened = backend_.open(config.backend, qp_count); !opened)
        return unexpected(opened.error());
    local_qps_.resize(backend_.qp_count());
    const std::vector<nds::transport::QpInfo> &local = backend_.local_qp_infos();
    for (std::size_t index = 0U; index < local.size(); ++index) {
        if (nds::transport::encode(&local[index], &local_qps_[index]) != nds::transport::CodecResult::Ok)
            return unexpected(ErrorCode::kTransport, "invalid transport endpoint record");
    }
    bootstrap_ = std::move(bootstrap);
    std::vector<nds::transport::QpInfo> peers(qp_count);
    if (qp_count == 1U) {
        nds::wire::QpInfo peer_wire{};
        if (const auto received = bootstrap_.receive_bytes(&peer_wire, sizeof(peer_wire)); !received)
            return unexpected(received.error());
        if (nds::transport::decode(&peer_wire, &peers.front()) != nds::transport::CodecResult::Ok)
            return unexpected(ErrorCode::kTransport, "invalid transport endpoint record");
    } else {
        nds::wire::QpInfoBatchHeader header{};
        if (const auto received = bootstrap_.receive_bytes(&header, sizeof(header)); !received)
            return unexpected(received.error());
        if (ntohl(header.magic) != nds::wire::kQpInfoBatchMagic || ntohs(header.version) != nds::wire::kQpInfoVersion ||
            ntohl(header.count) != qp_count) {
            return unexpected(ErrorCode::kTransport, "invalid or mismatched QP batch header");
        }
        std::vector<nds::wire::QpInfo> peer_wire(qp_count);
        if (const auto received = bootstrap_.receive_bytes(peer_wire.data(), peer_wire.size() * sizeof(peer_wire[0]));
            !received) {
            return unexpected(received.error());
        }
        for (std::size_t index = 0U; index < peer_wire.size(); ++index) {
            if (nds::transport::decode(&peer_wire[index], &peers[index]) != nds::transport::CodecResult::Ok)
                return unexpected(ErrorCode::kTransport, "invalid QP record in batch");
        }
    }
    if (const auto connected = backend_.connect(peers); !connected)
        return unexpected(connected.error());
    return {};
}

Result<RegisteredRegion> Transport::prepare_receive(void *buffer, std::size_t length) {
    return prepare_receive(0U, buffer, length);
}

Result<RegisteredRegion> Transport::prepare_receive(std::size_t qp_index, void *buffer, std::size_t length) {
    auto registered = backend_.register_memory(buffer, length, IBV_ACCESS_LOCAL_WRITE);
    if (!registered)
        return unexpected(registered.error());
    if (const auto posted = backend_.post_receive(qp_index, *registered); !posted)
        return unexpected(posted.error());
    return std::move(*registered);
}

Result<void> Transport::activate() {
    if (local_qps_.size() == 1U)
        return bootstrap_.send_bytes(local_qps_.data(), sizeof(local_qps_.front()));
    nds::wire::QpInfoBatchHeader header{};
    header.magic = htonl(nds::wire::kQpInfoBatchMagic);
    header.version = htons(nds::wire::kQpInfoVersion);
    header.count = htonl(static_cast<std::uint32_t>(local_qps_.size()));
    NDS_RETURN_IF_ERROR(bootstrap_.send_bytes(&header, sizeof(header)));
    return bootstrap_.send_bytes(local_qps_.data(), local_qps_.size() * sizeof(local_qps_.front()));
}
Result<void> Transport::receive(std::uint32_t timeout_ms) {
    return receive(0U, timeout_ms);
}
Result<void> Transport::receive(std::size_t qp_index, std::uint32_t timeout_ms) {
    if (const auto received = backend_.wait_receive(qp_index, timeout_ms); !received)
        return unexpected(received.error());
    return {};
}
Result<void> Transport::send(const RegisteredRegion &local, std::uint32_t length) {
    return send(0U, local, length);
}
Result<void> Transport::send(std::size_t qp_index, const RegisteredRegion &local, std::uint32_t length) {
    if (const auto sent = backend_.send(qp_index, local, length); !sent)
        return unexpected(sent.error());
    return {};
}
Result<RegisteredRegion> Transport::register_memory(void *buffer, std::size_t length, MemoryAccess access) {
    int backend_access = 0;
    if (access == MemoryAccess::LocalWrite)
        backend_access = IBV_ACCESS_LOCAL_WRITE;
    else if (access == MemoryAccess::RemoteRead)
        backend_access = IBV_ACCESS_REMOTE_READ;
    else if (access == MemoryAccess::RemoteWrite)
        backend_access = IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE;
    auto registered = backend_.register_memory(buffer, length, backend_access);
    if (!registered)
        return unexpected(registered.error());
    return std::move(*registered);
}
Result<void> Transport::read(const RegisteredRegion &local, std::uint64_t address, std::uint32_t key,
                             std::uint32_t length) {
    return read(0U, local, address, key, length);
}
Result<void> Transport::read(std::size_t qp_index, const RegisteredRegion &local, std::uint64_t address,
                             std::uint32_t key, std::uint32_t length) {
    if (const auto read = backend_.read(qp_index, local, address, key, length); !read)
        return unexpected(read.error());
    return {};
}
Result<void> Transport::write(const RegisteredRegion &local, std::uint64_t address, std::uint32_t key,
                              std::uint32_t length) {
    return write(0U, local, address, key, length);
}
Result<void> Transport::write(std::size_t qp_index, const RegisteredRegion &local, std::uint64_t address,
                              std::uint32_t key, std::uint32_t length) {
    if (const auto written = backend_.write(qp_index, local, address, key, length); !written)
        return unexpected(written.error());
    return {};
}
std::size_t Transport::qp_count() const noexcept {
    return backend_.qp_count();
}
TcpPeerExchange *Transport::bootstrap() noexcept {
    return &bootstrap_;
}

}  // namespace nds::server
