#ifndef NDS_CLIENT_TRANSPORT_HH
#define NDS_CLIENT_TRANSPORT_HH

#include "nds/npu_ra_context.hh"
#include "nds/npu_ra_qp.hh"
#include "nds/result.hh"
#include "nds/transport.hh"
#include "rma.hh"

#include <cstddef>
#include <cstdint>
#include <string>

namespace nds::client {

struct TransportConfig {
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
    friend class Transport;
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

private:
    friend class Transport;
    NpuRaQp *qp_{};
    nds_ra_mr_info info_{};
    void *handle_{};
};

struct RemoteRegion {
    std::uint64_t address{};
    std::uint64_t length{};
    std::uint32_t key{};
};

/* Owns the connected host control path and exposes registered-buffer operations. */
class Transport {
public:
    Result<void> open(const TransportConfig &config);
    Result<void> allocate(std::size_t size, DeviceBuffer *buffer);
    Result<void> register_memory(DeviceBuffer *buffer, RegisteredRegion *region);
    Result<void> copy_to_device(DeviceBuffer *buffer, const void *source, std::size_t size);
    Result<void> copy_from_device(void *destination, const DeviceBuffer &buffer, std::size_t size);
    Result<void> send(const RegisteredRegion &source, std::uint32_t length);
    Result<void> post_receive(const RegisteredRegion &destination, std::uint64_t wr_id);
    Result<std::uint32_t> poll(CompletionQueue queue, nds_ra_completion *completions,
                               std::uint32_t max_entries);
    Result<void> read(const RegisteredRegion &local, std::uint64_t remote_address, std::uint32_t remote_key,
                      std::uint32_t length);
    Result<void> write(const RegisteredRegion &local, std::uint64_t remote_address, std::uint32_t remote_key,
                       std::uint32_t length);
    Result<RemoteRegion> remote_region(const RegisteredRegion &region) const;
    TcpPeerExchange *bootstrap() noexcept;

    const nds_transport_endpoint &local_endpoint() const noexcept;
    Result<void> ready();

private:
    Result<void> post(WorkRequestOpcode opcode, const RegisteredRegion &local, std::uint64_t remote_address,
                      std::uint32_t remote_key, std::uint32_t length);
    Result<void> wait_for_send_completion(std::uint32_t timeout_ms);

    TransportConfig config_{};
    NpuRaContext context_;
    NpuRaQp qp_;
    DeviceBuffer send_wr_ids_;
    DeviceBuffer receive_wr_ids_;
    TcpPeerExchange bootstrap_;
    nds_transport_endpoint local_{};
};

}  // namespace nds::client

#endif
