#ifndef NDS_SERVER_TRANSPORT_HH
#define NDS_SERVER_TRANSPORT_HH

#include "backend.hh"
#include "nds/result.hh"
#include "nds/connection.hh"

#include <cstdint>
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
    TcpPeerExchange *bootstrap() noexcept;

private:
    VerbsBackend backend_;
    TcpPeerExchange bootstrap_;
    nds_qp_info_wire local_wire_{};
};

}  // namespace nds::server
#endif
