#ifndef NDS_SERVER_TRANSPORT_HH
#define NDS_SERVER_TRANSPORT_HH

#include "backend.hh"
#include "nds/result.hh"
#include "nds/tcp_bootstrap.hh"

#include <cstdint>
#include <span>
#include <string>

namespace nds::server {

enum class MemoryAccess {
    LocalRead,
    LocalWrite,
    RemoteRead,
    RemoteWrite,
};

struct ConnectionConfig {
    BackendConfig backend;
    std::string listen_address{"0.0.0.0:18515"};
};

class Connection {
public:
    Result<void> open(const ConnectionConfig &config);
    Result<RegisteredRegion> prepare_receive(void *buffer, std::size_t length);
    Result<void> activate();
    Result<void> receive(std::uint32_t timeout_ms);
    Result<void> send(const RegisteredRegion &local, std::uint32_t length);
    Result<RegisteredRegion> register_memory(void *buffer, std::size_t length, MemoryAccess access);
    Result<void> read(const RegisteredRegion &local, std::uint64_t remote_address, std::uint32_t remote_key,
                      std::uint32_t length);
    Result<void> write(const RegisteredRegion &local, std::uint64_t remote_address, std::uint32_t remote_key,
                       std::uint32_t length);
    Result<void> read_window(const RegisteredRegion &local, std::uint64_t remote_address, std::uint32_t remote_key,
                             std::uint32_t length, std::uint32_t request_count);
    Result<void> write_window(const RegisteredRegion &local, std::uint64_t remote_address, std::uint32_t remote_key,
                              std::uint32_t length, std::uint32_t request_count);
    Result<void> read_window_offsets(const RegisteredRegion &local, std::uint64_t remote_address,
                                     std::uint32_t remote_key, std::uint32_t length,
                                     std::span<const std::uint64_t> offsets, std::uint32_t post_batch);
    Result<void> write_window_offsets(const RegisteredRegion &local, std::uint64_t remote_address,
                                      std::uint32_t remote_key, std::uint32_t length,
                                      std::span<const std::uint64_t> offsets, std::uint32_t post_batch);
    TcpPeerExchange *bootstrap() noexcept;

private:
    VerbsBackend backend_;
    TcpPeerExchange bootstrap_;
    nds::wire::QpInfo local_wire_{};
};

}  // namespace nds::server
#endif
