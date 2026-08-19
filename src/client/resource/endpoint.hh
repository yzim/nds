#ifndef NDS_CLIENT_ENDPOINT_HH
#define NDS_CLIENT_ENDPOINT_HH

#include "nds/connection.h"
#include "nds/device_transport.h"
#include "nds/ra_loader.h"
#include "nds/result.hh"

#include <cstdint>
#include <string>

namespace nds::client {

class MemoryBuffer;
class Runtime;
struct EndpointTestAccess;

enum class NpuExecutionMode {
    Ra,
    Aicpu,
    Aiv,
};

enum QueuePairControlFlag : std::uint32_t {
    QueuePairCallerPollsCq = 1U << 0,
};

enum class MemoryAccess : int {
    LocalWrite = NDS_RA_ACCESS_LOCAL_WRITE,
    RemoteWrite = NDS_RA_ACCESS_REMOTE_WRITE,
    RemoteRead = NDS_RA_ACCESS_REMOTE_READ,
    DirectNpu = NDS_RA_ACCESS_DIRECT_NPU,
};

struct EndpointConfig {
    std::string ra_library;
    std::uint32_t physical_device_id{NDS_RA_PHY_ID_NPU0};
    std::int32_t hdc_type{NDS_RA_HDC_SERVICE_TYPE_RDMA_V2};
    std::string local_ipv4;
};

struct QueuePairConfig {
    std::uint16_t port_num{1};
    std::uint16_t path_mtu{1024};
    std::uint32_t traffic_class{};
    std::uint32_t service_level{};
    std::uint32_t retry_count{7};
    std::uint32_t retry_timeout{14};
    int ai_qp_mode{-1};
    std::uint32_t send_queue_depth{32768};
    std::uint32_t receive_queue_depth{128};
    std::uint32_t control_flags{QueuePairCallerPollsCq};
};

class Endpoint;

class MemoryRegion {
public:
    MemoryRegion() = default;
    ~MemoryRegion();
    MemoryRegion(const MemoryRegion &) = delete;
    MemoryRegion &operator=(const MemoryRegion &) = delete;
    MemoryRegion(MemoryRegion &&other) noexcept;
    MemoryRegion &operator=(MemoryRegion &&other) noexcept;

    std::uint64_t address() const noexcept;
    std::uint32_t local_key() const noexcept;
    std::uint32_t remote_key() const noexcept;
    std::uint64_t length() const noexcept;
    bool belongs_to(const Endpoint *endpoint) const noexcept;

private:
    friend class Endpoint;
    friend struct EndpointTestAccess;
    void reset() noexcept;

    Endpoint *endpoint_{};
    const MemoryBuffer *buffer_{};
    nds_ra_mr_info info_{};
    void *handle_{};
};

class QueuePair {
public:
    QueuePair() = default;
    ~QueuePair();
    QueuePair(const QueuePair &) = delete;
    QueuePair &operator=(const QueuePair &) = delete;
    QueuePair(QueuePair &&other) noexcept;
    QueuePair &operator=(QueuePair &&other) noexcept;

    Result<nds_qp_info> local_qp_info() const;
    Result<void> connect(const nds_qp_info &peer);
    Result<void> query_port_status(int *status);
    Result<void> query_support_lite(int *support_lite);
    Result<void> query_status(int *status);
    Result<void> query_cqe_errors(nds_ra_cqe_error *errors, std::uint32_t *count);
    Result<void> set_device_wr_id_storage(std::uint64_t send_address, std::uint64_t receive_address);
    Result<nds_device_transport> make_device_transport() const;

    bool created() const noexcept;
    bool connected() const noexcept;
    const nds_ra_qp_attr &local_attributes() const noexcept;
    NpuExecutionMode execution_mode() const noexcept;

    /* Temporary RA execution accessors; replaced by explicit execution views next. */
    nds_ra_api *ra_api() const noexcept;
    void *handle() const noexcept;
    nds_ra_sge *posted_send_sge() noexcept;

private:
    friend class Endpoint;
    QueuePair(Endpoint *endpoint, const QueuePairConfig &config, NpuExecutionMode execution);
    Result<void> initialize();
    void reset() noexcept;
    Result<nds_ra_typical_qp> build_typical_qp(const nds_ra_qp_attr &attributes, std::uint32_t traffic_class,
                                               std::uint32_t service_level, std::uint32_t retry_count,
                                               std::uint32_t retry_timeout) const;

    Endpoint *endpoint_{};
    QueuePairConfig config_{};
    NpuExecutionMode execution_{NpuExecutionMode::Ra};
    void *handle_{};
    nds_ra_qp_attr local_attributes_{};
    nds_ra_ai_qp_info ai_qp_info_{};
    std::uint64_t send_wr_ids_{};
    std::uint64_t receive_wr_ids_{};
    nds_ra_sge posted_send_sge_{};
    bool connected_{};
};

class Endpoint {
public:
    Endpoint() = default;
    ~Endpoint();
    Endpoint(const Endpoint &) = delete;
    Endpoint &operator=(const Endpoint &) = delete;

    Result<void> open(Runtime *runtime, const EndpointConfig &config);
    Result<QueuePair> create_qp(const QueuePairConfig &config,
                                NpuExecutionMode execution = NpuExecutionMode::Ra);
    Result<MemoryRegion> reg_mr(const MemoryBuffer &buffer, MemoryAccess access);
    bool opened() const noexcept;

private:
    friend class MemoryRegion;
    friend class QueuePair;
    friend struct EndpointTestAccess;
    Result<void> deregister(void *handle);
    void reset() noexcept;

    Runtime *runtime_{};
    EndpointConfig config_{};
    nds_ra_api api_{};
    void *rdev_handle_{};
    bool ra_initialized_{};
};

}  // namespace nds::client

#endif
