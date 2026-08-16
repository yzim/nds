#ifndef NDS_CLIENT_CONNECTION_HH
#define NDS_CLIENT_CONNECTION_HH

#include "nds/npu_ra_context.hh"
#include "nds/npu_ra_qp.hh"
#include "nds/result.hh"
#include "nds/connection.hh"

#include <cstddef>
#include <cstdint>
#include <string>

namespace nds::client {

/* Host-example paths used to invoke AIV/AICPU operators. Not qp control. */
struct RmaConfig {
    std::string aicpu_kernel_config;
    std::string aiv_kernel;
};

struct ConnectionConfig {
    NpuExecutionMode execution{NpuExecutionMode::HostRa};
    NpuRaContextConfig context;
    NpuRaQpConfig qp;
    RmaConfig rma;
    std::string cpu_ipv4;
    std::uint16_t tcp_port{};
    std::uint32_t tcp_timeout_ms{};
};

class DeviceBuffer {
public:
    DeviceBuffer() = default;
    ~DeviceBuffer();
    DeviceBuffer(const DeviceBuffer &) = delete;
    DeviceBuffer &operator=(const DeviceBuffer &) = delete;

    void *data() const noexcept;
    std::size_t size() const noexcept;

private:
    friend class Connection;
    NpuRaContext *context_{};
    void *data_{};
    std::size_t size_{};
};

class RegisteredRegion {
public:
    RegisteredRegion() = default;
    ~RegisteredRegion();
    RegisteredRegion(const RegisteredRegion &) = delete;
    RegisteredRegion &operator=(const RegisteredRegion &) = delete;

    std::uint64_t address() const noexcept;
    std::uint64_t length() const noexcept;
    std::uint32_t local_key() const noexcept;
    std::uint32_t remote_key() const noexcept;
    bool belongs_to(const NpuRaQp *qp) const noexcept;

private:
    friend class Connection;
    NpuRaQp *qp_{};
    nds_ra_mr_info info_{};
    void *handle_{};
};

struct RemoteRegion {
    std::uint64_t address{};
    std::uint64_t length{};
    std::uint32_t key{};
};

/* Connection control: context, QP/MR, TCP endpoint exchange. Does not post. */
class Connection {
public:
    Result<void> open(const ConnectionConfig &config);
    Result<void> allocate(std::size_t size, DeviceBuffer *buffer);
    Result<void> register_memory(DeviceBuffer *buffer, RegisteredRegion *region);
    Result<void> copy_to_device(DeviceBuffer *buffer, const void *source, std::size_t size);
    Result<void> copy_from_device(void *destination, const DeviceBuffer &buffer, std::size_t size);
    Result<RemoteRegion> remote_region(const RegisteredRegion &region) const;
    TcpPeerExchange *bootstrap() noexcept;

    const nds_qp_info &local_qp_info() const noexcept;
    Result<void> ready();

    NpuRaContext *context() noexcept;
    NpuRaQp *qp() noexcept;
    const ConnectionConfig &config() const noexcept;

private:
    ConnectionConfig config_{};
    NpuRaContext context_;
    NpuRaQp qp_;
    DeviceBuffer send_wr_ids_;
    DeviceBuffer receive_wr_ids_;
    TcpPeerExchange bootstrap_;
    nds_qp_info local_{};
};

}  // namespace nds::client

#endif
