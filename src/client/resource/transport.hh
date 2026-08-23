#ifndef NDS_CLIENT_TRANSPORT_HH
#define NDS_CLIENT_TRANSPORT_HH

#include "endpoint.hh"
#include "runtime.hh"

#include "nds/tcp_bootstrap.hh"
#include "nds/result.hh"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace nds::client {

struct TransportConfig {
    EndpointConfig endpoint;
    QueuePairConfig qp;
    std::uint32_t qp_count{1U};
    std::string server_address;
    std::string listen_address;
    std::uint32_t tcp_timeout_ms{};
};

struct ExecutionConfig {
    NpuExecutionMode mode{NpuExecutionMode::Ra};
    std::string aicpu_kernel_config;
    std::string aiv_kernel;
};

/* Owns one endpoint and a connected set of QPs; it exposes no data-plane operations. */
class Transport {
public:
    Result<void> open(Runtime *runtime, const TransportConfig &config, const ExecutionConfig &execution);
    Result<void> open_server(Runtime *runtime, const TransportConfig &config, const ExecutionConfig &execution);

    TcpPeerExchange *bootstrap() noexcept;
    const nds::transport::QpInfo &local_qp_info() const noexcept;
    const std::vector<nds::transport::QpInfo> &local_qp_infos() const noexcept;
    Result<void> ready();

    Runtime *runtime() noexcept;
    Endpoint *endpoint() noexcept;
    QueuePair *qp() noexcept;
    QueuePair *qp(std::size_t index) noexcept;
    const std::vector<QueuePair> &qps() const noexcept;
    std::size_t qp_count() const noexcept;
    const ExecutionConfig &execution() const noexcept;

private:
    Result<void> open_common(Runtime *runtime, const TransportConfig &config, const ExecutionConfig &execution,
                             bool server);
    Result<void> initialize_private_memory();

    Runtime *runtime_{};
    TransportConfig config_{};
    ExecutionConfig execution_{};
    Endpoint endpoint_;
    std::vector<QueuePair> qps_;
    std::vector<MemoryBuffer> send_wr_ids_;
    std::vector<MemoryBuffer> receive_wr_ids_;
    TcpPeerExchange bootstrap_;
    std::vector<nds::transport::QpInfo> local_qps_;
};

}  // namespace nds::client

#endif
