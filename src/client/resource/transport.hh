#ifndef NDS_CLIENT_TRANSPORT_HH
#define NDS_CLIENT_TRANSPORT_HH

#include "memory.hh"
#include "runtime.hh"

#include "nds/connection.hh"
#include "nds/npu_ra_qp.hh"
#include "nds/result.hh"

#include <cstddef>
#include <cstdint>
#include <string>

namespace nds::client {

struct TransportConfig {
    NpuRaQpConfig qp;
    std::string cpu_ipv4;
    std::uint16_t tcp_port{};
    std::uint32_t tcp_timeout_ms{};
};

struct ExecutionConfig {
    NpuExecutionMode mode{NpuExecutionMode::Ra};
    std::string aicpu_kernel_config;
    std::string aiv_kernel;
};

/* Owns the NPU-to-one-CPU-peer QP, bootstrap, and private transport buffers. */
class Transport {
public:
    Result<void> open(NpuRuntime *runtime, const TransportConfig &config, const ExecutionConfig &execution);

    Result<void> read(LocalAddress local, RemoteAddress remote, std::uint32_t length);
    Result<void> write(LocalAddress local, RemoteAddress remote, std::uint32_t length);
    Result<void> send_bytes(const void *source, std::size_t size);

    TcpPeerExchange *bootstrap() noexcept;
    const nds_qp_info &local_qp_info() const noexcept;
    Result<void> ready();

    NpuRuntime *runtime() noexcept;
    NpuRaQp *qp() noexcept;
    const ExecutionConfig &execution() const noexcept;

private:
    Result<void> initialize_private_memory();

    NpuRuntime *runtime_{};
    TransportConfig config_{};
    ExecutionConfig execution_{};
    NpuRaQp qp_;
    DeviceBuffer send_buffer_;
    DeviceBuffer receive_buffer_;
    RegisteredRegion send_region_;
    RegisteredRegion receive_region_;
    DeviceBuffer send_wr_ids_;
    DeviceBuffer receive_wr_ids_;
    TcpPeerExchange bootstrap_;
    nds_qp_info local_{};
    std::uint64_t next_wr_id_{1U};
};

}  // namespace nds::client

#endif
