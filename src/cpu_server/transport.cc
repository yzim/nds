#include "transport.hh"

#include "nds/transport.h"

#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <sys/socket.h>
#include <unistd.h>

namespace nds::cpu {

bool Connection::open(const ConnectionConfig &config, std::string *error) {
    if (error == nullptr || !backend_.open(config.backend, error))
        return false;
    char codec_error[NDS_TRANSPORT_ERROR_CAPACITY]{};
    if (nds_transport_endpoint_encode(&backend_.local_endpoint(), &local_wire_, codec_error) != 0) {
        *error = codec_error;
        return false;
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
        *error = std::strerror(errno);
        return false;
    }
    const int peer_fd = accept(listener, nullptr, nullptr);
    (void)close(listener);
    if (peer_fd < 0) {
        *error = std::strerror(errno);
        return false;
    }
    if (!bootstrap_.adopt(peer_fd, error)) {
        (void)close(peer_fd);
        return false;
    }
    nds_transport_endpoint_wire peer_wire{};
    nds_transport_endpoint peer{};
    if (!bootstrap_.receive_bytes(&peer_wire, sizeof(peer_wire), error) ||
        nds_transport_endpoint_decode(&peer_wire, &peer, codec_error) != 0 || !backend_.connect(peer, error)) {
        if (error->empty())
            *error = codec_error;
        return false;
    }
    return true;
}

bool Connection::prepare_receive(void *buffer, std::size_t length, RegisteredRegion *region, std::string *error) {
    return backend_.register_memory(buffer, length, IBV_ACCESS_LOCAL_WRITE, region, error) &&
           backend_.post_receive(*region, error);
}

bool Connection::activate(std::string *error) {
    return bootstrap_.send_bytes(&local_wire_, sizeof(local_wire_), error);
}
bool Connection::receive(std::uint32_t timeout_ms, std::string *error) {
    return backend_.wait_receive(timeout_ms, error);
}
bool Connection::register_memory(void *buffer, std::size_t length, MemoryAccess access, RegisteredRegion *region,
                                 std::string *error) {
    const int backend_access = access == MemoryAccess::LocalWrite ? IBV_ACCESS_LOCAL_WRITE : 0;
    return backend_.register_memory(buffer, length, backend_access, region, error);
}
bool Connection::read(const RegisteredRegion &local, std::uint64_t address, std::uint32_t key, std::uint32_t length,
                      std::string *error) {
    return backend_.read(local, address, key, length, error);
}
bool Connection::write(const RegisteredRegion &local, std::uint64_t address, std::uint32_t key, std::uint32_t length,
                       std::string *error) {
    return backend_.write(local, address, key, length, error);
}
TcpPeerExchange *Connection::bootstrap() noexcept {
    return &bootstrap_;
}

}  // namespace nds::cpu
