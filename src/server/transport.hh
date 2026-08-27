#ifndef NDS_SERVER_TRANSPORT_HH
#define NDS_SERVER_TRANSPORT_HH

#include "backend.hh"
#include "nds/result.hh"
#include "nds/tcp_bootstrap.hh"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace nds::server {

enum class MemoryAccess {
    LocalRead,
    LocalWrite,
    RemoteRead,
    RemoteWrite,
};

struct TransportConfig {
    BackendConfig backend;
    std::uint32_t qp_count{1U};
    std::string listen_address{"0.0.0.0:18515"};
};

class Transport {
public:
    Result<void> open(const TransportConfig &config);
    Result<RegisteredRegion> prepare_receive(void *buffer, std::size_t length);
    Result<RegisteredRegion> prepare_receive(std::size_t qp_index, void *buffer, std::size_t length);
    Result<void> activate();
    Result<void> receive(std::uint32_t timeout_ms);
    Result<void> receive(std::size_t qp_index, std::uint32_t timeout_ms);
    Result<void> send(const RegisteredRegion &local, std::uint32_t length);
    Result<void> send(std::size_t qp_index, const RegisteredRegion &local, std::uint32_t length);
    Result<RegisteredRegion> register_memory(void *buffer, std::size_t length, MemoryAccess access);
    Result<void> read(const RegisteredRegion &local, std::uint64_t remote_address, std::uint32_t remote_key,
                      std::uint32_t length);
    Result<void> read(std::size_t qp_index, const RegisteredRegion &local, std::uint64_t remote_address,
                      std::uint32_t remote_key, std::uint32_t length);
    Result<void> write(const RegisteredRegion &local, std::uint64_t remote_address, std::uint32_t remote_key,
                       std::uint32_t length);
    Result<void> write(std::size_t qp_index, const RegisteredRegion &local, std::uint64_t remote_address,
                       std::uint32_t remote_key, std::uint32_t length);
    std::size_t qp_count() const noexcept;
    TcpPeerExchange *bootstrap() noexcept;

private:
    VerbsBackend backend_;
    TcpPeerExchange bootstrap_;
    std::vector<nds::wire::QpInfo> local_qps_;
};

}  // namespace nds::server
#endif
