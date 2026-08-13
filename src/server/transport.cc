#include "transport.hh"

#include "nds/transport.h"

#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <sys/socket.h>
#include <unistd.h>

namespace nds::server {

Result<void> Connection::open(const ConnectionConfig &config) {
    std::string error;
    if (!backend_.open(config.backend, &error))
        return failure(ErrorCode::kVerbs, std::move(error));
    char codec_error[NDS_TRANSPORT_ERROR_CAPACITY]{};
    if (nds_transport_endpoint_encode(&backend_.local_endpoint(), &local_wire_, codec_error) != 0) {
        return failure(ErrorCode::kTransport, codec_error);
    }
    const int listener = socket(AF_INET, SOCK_STREAM, 0);
    int enabled = 1;
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(config.tcp_port);
    if (listener < 0 || setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &enabled, sizeof(enabled)) != 0 ||
        inet_pton(AF_INET, config.listen_address.c_str(), &address.sin_addr) != 1 ||
        bind(listener, reinterpret_cast<const sockaddr *>(&address), sizeof(address)) != 0 ||
        listen(listener, 1) != 0) {
        if (listener >= 0)
            (void)close(listener);
        return failure(ErrorCode::kTransport, std::strerror(errno));
    }
    const int peer_fd = accept(listener, nullptr, nullptr);
    (void)close(listener);
    if (peer_fd < 0) {
        return failure(ErrorCode::kTransport, std::strerror(errno));
    }
    if (!bootstrap_.adopt(peer_fd, &error)) {
        (void)close(peer_fd);
        return failure(ErrorCode::kTransport, std::move(error));
    }
    nds_transport_endpoint_wire peer_wire{};
    nds_transport_endpoint peer{};
    if (!bootstrap_.receive_bytes(&peer_wire, sizeof(peer_wire), &error)) {
        return failure(ErrorCode::kTransport, std::move(error));
    }
    if (nds_transport_endpoint_decode(&peer_wire, &peer, codec_error) != 0) {
        return failure(ErrorCode::kTransport, codec_error);
    }
    if (!backend_.connect(peer, &error)) {
        return failure(ErrorCode::kVerbs, std::move(error));
    }
    return {};
}

Result<void> Connection::prepare_receive(void *buffer, std::size_t length, RegisteredRegion *region) {
    std::string error;
    if (!backend_.register_memory(buffer, length, IBV_ACCESS_LOCAL_WRITE, region, &error) ||
        !backend_.post_receive(*region, &error)) {
        return failure(ErrorCode::kVerbs, std::move(error));
    }
    return {};
}

Result<void> Connection::activate() {
    std::string error;
    if (!bootstrap_.send_bytes(&local_wire_, sizeof(local_wire_), &error)) {
        return failure(ErrorCode::kTransport, std::move(error));
    }
    return {};
}
Result<void> Connection::receive(std::uint32_t timeout_ms) {
    std::string error;
    if (!backend_.wait_receive(timeout_ms, &error)) {
        return failure(ErrorCode::kVerbs, std::move(error));
    }
    return {};
}
Result<void> Connection::register_memory(void *buffer, std::size_t length, MemoryAccess access,
                                         RegisteredRegion *region) {
    const int backend_access = access == MemoryAccess::LocalWrite ? IBV_ACCESS_LOCAL_WRITE : 0;
    std::string error;
    if (!backend_.register_memory(buffer, length, backend_access, region, &error)) {
        return failure(ErrorCode::kVerbs, std::move(error));
    }
    return {};
}
Result<void> Connection::read(const RegisteredRegion &local, std::uint64_t address, std::uint32_t key,
                              std::uint32_t length) {
    std::string error;
    if (!backend_.read(local, address, key, length, &error)) {
        return failure(ErrorCode::kVerbs, std::move(error));
    }
    return {};
}
Result<void> Connection::write(const RegisteredRegion &local, std::uint64_t address, std::uint32_t key,
                               std::uint32_t length) {
    std::string error;
    if (!backend_.write(local, address, key, length, &error)) {
        return failure(ErrorCode::kVerbs, std::move(error));
    }
    return {};
}
TcpPeerExchange *Connection::bootstrap() noexcept {
    return &bootstrap_;
}

}  // namespace nds::server
