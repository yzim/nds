#ifndef NDS_CLIENT_ENDPOINT_HH
#define NDS_CLIENT_ENDPOINT_HH

#include "transport_protocol.hh"
#include "device_transport.h"
#include "loaders/ra_loader.hh"
#include "result.hh"

#include <cstdint>
#include <string>
#include <vector>

namespace nds::client {

class Runtime;
class MemoryBuffer;
struct EndpointTestAccess;
enum class BackendMode;

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
    std::int32_t hdc_type{NDS_RA_HDC_SERVICE_TYPE_RDMA_V2};
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
    // HCCP consumes AI-QP completions unless a future caller-owned path opts in.
    std::uint32_t control_flags{};
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
    NdsRaMrInfo info_{};
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

    Result<nds::transport::QpInfo> local_qp_info() const;
    Result<void> connect(const nds::transport::QpInfo &peer);
    Result<int> query_port_status();
    Result<int> query_support_lite();
    Result<int> query_status();
    Result<std::vector<NdsRaCqeError>> query_cqe_errors();

    bool created() const noexcept;
    bool connected() const noexcept;
    const NdsRaQpAttr &local_attributes() const noexcept;
    BackendMode backend_mode() const noexcept;

    /* RA provider state remains owned by the endpoint and is accessed by the RA backend layer. */
    NdsRaApi *ra_api() const noexcept;
    void *handle() const noexcept;
    NdsRaSge *posted_send_sge() noexcept;

private:
    friend class Endpoint;
    friend class Transport;
    friend class BackendLauncher;
    QueuePair(Endpoint *endpoint, const QueuePairConfig &config, BackendMode mode);
    Result<void> initialize();
    void reset() noexcept;
    Result<NdsRaTypicalQp> build_typical_qp(const NdsRaQpAttr &attributes, std::uint32_t traffic_class,
                                            std::uint32_t service_level, std::uint32_t retry_count,
                                            std::uint32_t retry_timeout) const;

    Endpoint *endpoint_{};
    QueuePairConfig config_{};
    // Records the backend mode that selected this QP's creation and dataplane layout.
    BackendMode mode_;
    void *handle_{};
    NdsRaQpAttr local_attributes_{};
    NdsRaAiQpInfo ai_qp_info_{};
    // Raw AIV CQEs identify a queue slot, not the submitted work request.
    // These device-visible tables recover the request ID at completion time.
    // They are deliberately private: provider-backed RA and AICPU own their
    // corresponding bookkeeping internally.
    MemoryBuffer send_wr_ids_;
    MemoryBuffer receive_wr_ids_;
    NdsRaSge posted_send_sge_{};
    bool connected_{};
};

class Endpoint {
public:
    Endpoint() = default;
    ~Endpoint();
    Endpoint(const Endpoint &) = delete;
    Endpoint &operator=(const Endpoint &) = delete;
    Endpoint(Endpoint &&other) noexcept;
    Endpoint &operator=(Endpoint &&other) noexcept;

    Result<QueuePair> create_qp(const QueuePairConfig &config, BackendMode backend);
    Result<MemoryRegion> reg_mr(const MemoryBuffer &buffer, MemoryAccess access);
    bool opened() const noexcept;

private:
    friend class Runtime;
    friend class MemoryRegion;
    friend class QueuePair;
    friend struct EndpointTestAccess;
    Result<void> open(Runtime *runtime, const EndpointConfig &config);
    Result<void> deregister(void *handle);
    void reset() noexcept;

    Runtime *runtime_{};
    EndpointConfig config_{};
    std::uint32_t physical_device_id_{};
    NdsRaApi api_{};
    void *rdev_handle_{};
    bool ra_initialized_{};
};

}  // namespace nds::client

#endif
