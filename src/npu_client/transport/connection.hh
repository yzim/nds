#ifndef NDS_NPU_CONNECTION_HH
#define NDS_NPU_CONNECTION_HH

#include "backend/backend.hh"
#include "nds/npu_ra_context.hh"
#include "nds/npu_ra_qp.hh"
#include "nds/peer_exchange.hh"

#include <cstddef>
#include <cstdint>
#include <string>

namespace nds::npu {

struct ConnectionConfig {
    NpuRaContextConfig context;
    NpuRaQpConfig qp;
    BackendConfig backend;
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

class Connection {
public:
    bool open(const ConnectionConfig &config, std::string *error);
    bool allocate(std::size_t size, DeviceBuffer *buffer, std::string *error);
    bool register_memory(DeviceBuffer *buffer, RegisteredRegion *region, std::string *error);
    bool copy_to_device(DeviceBuffer *buffer, const void *source, std::size_t size, std::string *error);
    bool copy_from_device(void *destination, const DeviceBuffer &buffer, std::size_t size, std::string *error);
    bool send(const RegisteredRegion &source, std::uint32_t length, std::string *error);
    bool remote_region(const RegisteredRegion &region, RemoteRegion *remote, std::string *error) const;
    TcpPeerExchange *bootstrap() noexcept;

    const nds_rc_endpoint &local_endpoint() const noexcept;
    bool ready(std::string *error);

private:
    ConnectionConfig config_{};
    NpuRaContext context_;
    NpuRaQp qp_;
    TcpPeerExchange bootstrap_;
    nds_rc_endpoint local_{};
};

}  // namespace nds::npu

#endif
