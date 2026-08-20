#ifndef NDS_CLIENT_TRANSPORT_HH
#define NDS_CLIENT_TRANSPORT_HH

#include "endpoint.hh"
#include "runtime.hh"

#include "nds/tcp_bootstrap.hh"
#include "nds/result.hh"

#include <cstdint>
#include <string>

namespace nds::client {

struct TransportConfig {
    EndpointConfig endpoint;
    QueuePairConfig qp;
    std::string server_address;
    std::uint32_t tcp_timeout_ms{};
};

struct ExecutionConfig {
    NpuExecutionMode mode{NpuExecutionMode::Ra};
    std::string aicpu_kernel_config;
    std::string aiv_kernel;
};

/* Owns and connects one endpoint/QP pair; it exposes no data-plane operations. */
class Transport {
public:
    Result<void> open(Runtime *runtime, const TransportConfig &config, const ExecutionConfig &execution);

    TcpPeerExchange *bootstrap() noexcept;
    const nds::transport::QpInfo &local_qp_info() const noexcept;
    Result<void> ready();

    Runtime *runtime() noexcept;
    Endpoint *endpoint() noexcept;
    QueuePair *qp() noexcept;
    const ExecutionConfig &execution() const noexcept;

private:
    Result<void> initialize_private_memory();

    Runtime *runtime_{};
    TransportConfig config_{};
    ExecutionConfig execution_{};
    Endpoint endpoint_;
    QueuePair qp_;
    MemoryBuffer send_wr_ids_;
    MemoryBuffer receive_wr_ids_;
    TcpPeerExchange bootstrap_;
    nds::transport::QpInfo local_{};
};

}  // namespace nds::client

#endif
