#include "transport.hh"

#include "nds/wire/transport.hh"

#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <sys/socket.h>
#include <unistd.h>
#include <utility>

namespace nds::server {

Result<void> Connection::open(const ConnectionConfig &config) {
    if (const auto opened = backend_.open(config.backend); !opened)
        return unexpected(opened.error());
    if (nds::transport::encode(&backend_.local_qp_info(), &local_wire_) != nds::transport::CodecResult::Ok) {
        return unexpected(ErrorCode::kTransport, "invalid transport endpoint record");
    }
    const int listener = socket(AF_INET, SOCK_STREAM, 0);
    int enabled = 1;
    sockaddr_in address{};
    const auto listen_address = parse_tcp_address(config.listen_address);
    if (!listen_address)
        return unexpected(listen_address.error());
    address.sin_family = AF_INET;
    address.sin_port = htons(listen_address->port);
    if (listener < 0 || setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &enabled, sizeof(enabled)) != 0 ||
        inet_pton(AF_INET, listen_address->ipv4.c_str(), &address.sin_addr) != 1 ||
        bind(listener, reinterpret_cast<const sockaddr *>(&address), sizeof(address)) != 0 ||
        listen(listener, 1) != 0) {
        if (listener >= 0)
            (void)close(listener);
        return unexpected(ErrorCode::kTransport, std::strerror(errno));
    }
    const int peer_fd = accept(listener, nullptr, nullptr);
    (void)close(listener);
    if (peer_fd < 0) {
        return unexpected(ErrorCode::kTransport, std::strerror(errno));
    }
    if (const auto adopted = bootstrap_.adopt(peer_fd); !adopted) {
        (void)close(peer_fd);
        return unexpected(adopted.error());
    }
    nds::wire::QpInfo peer_wire{};
    nds::transport::QpInfo peer{};
    if (const auto received = bootstrap_.receive_bytes(&peer_wire, sizeof(peer_wire)); !received)
        return unexpected(received.error());
    if (nds::transport::decode(&peer_wire, &peer) != nds::transport::CodecResult::Ok) {
        return unexpected(ErrorCode::kTransport, "invalid transport endpoint record");
    }
    if (const auto connected = backend_.connect(peer); !connected)
        return unexpected(connected.error());
    return {};
}

Result<RegisteredRegion> Connection::prepare_receive(void *buffer, std::size_t length) {
    auto registered = backend_.register_memory(buffer, length, IBV_ACCESS_LOCAL_WRITE);
    if (!registered)
        return unexpected(registered.error());
    if (const auto posted = backend_.post_receive(*registered); !posted)
        return unexpected(posted.error());
    return std::move(*registered);
}

Result<void> Connection::activate() {
    return bootstrap_.send_bytes(&local_wire_, sizeof(local_wire_));
}
Result<void> Connection::receive(std::uint32_t timeout_ms) {
    if (const auto received = backend_.wait_receive(timeout_ms); !received)
        return unexpected(received.error());
    return {};
}
Result<void> Connection::send(const RegisteredRegion &local, std::uint32_t length) {
    if (const auto sent = backend_.send(local, length); !sent)
        return unexpected(sent.error());
    return {};
}
Result<RegisteredRegion> Connection::register_memory(void *buffer, std::size_t length, MemoryAccess access) {
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
Result<void> Connection::read(const RegisteredRegion &local, std::uint64_t address, std::uint32_t key,
                              std::uint32_t length) {
    if (const auto read = backend_.read(local, address, key, length); !read)
        return unexpected(read.error());
    return {};
}
Result<void> Connection::write(const RegisteredRegion &local, std::uint64_t address, std::uint32_t key,
                               std::uint32_t length) {
    if (const auto written = backend_.write(local, address, key, length); !written)
        return unexpected(written.error());
    return {};
}
TcpPeerExchange *Connection::bootstrap() noexcept {
    return &bootstrap_;
}

}  // namespace nds::server
