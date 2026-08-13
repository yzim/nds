#ifndef NDS_CPU_TRANSPORT_HH
#define NDS_CPU_TRANSPORT_HH

#include "backend.hh"
#include "nds/transport.hh"

#include <cstdint>
#include <string>

namespace nds::cpu {

enum class MemoryAccess {
    LocalRead,
    LocalWrite,
};

struct ConnectionConfig {
    BackendConfig backend;
    std::string listen_address{"0.0.0.0"};
    std::uint16_t tcp_port{18515U};
};

class Connection {
public:
    bool open(const ConnectionConfig &config, std::string *error);
    bool prepare_receive(void *buffer, std::size_t length, RegisteredRegion *region, std::string *error);
    bool activate(std::string *error);
    bool receive(std::uint32_t timeout_ms, std::string *error);
    bool register_memory(void *buffer, std::size_t length, MemoryAccess access, RegisteredRegion *region,
                         std::string *error);
    bool read(const RegisteredRegion &local, std::uint64_t remote_address, std::uint32_t remote_key,
              std::uint32_t length, std::string *error);
    bool write(const RegisteredRegion &local, std::uint64_t remote_address, std::uint32_t remote_key,
               std::uint32_t length, std::string *error);
    TcpPeerExchange *bootstrap() noexcept;

private:
    VerbsBackend backend_;
    TcpPeerExchange bootstrap_;
    nds_transport_endpoint_wire local_wire_{};
};

}  // namespace nds::cpu
#endif
