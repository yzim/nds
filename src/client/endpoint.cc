#include "endpoint.hh"

#include "loaders/dsmi_loader.hh"
#include "runtime.hh"

#include <arpa/inet.h>

#include <cstring>
#include <limits>
#include <utility>

namespace nds::client {
namespace {

constexpr std::uint32_t kQpnMask = 0x00ffffffU;
constexpr std::uint32_t kPsnMask = 0x00ffffffU;
constexpr unsigned int kMaxCqeErrors = 512U;

bool is_valid_qp_number(std::uint32_t value) {
    return value != 0U && value <= kQpnMask;
}

bool is_valid_psn(std::uint32_t value) {
    return value <= kPsnMask;
}

}  // namespace

MemoryRegion::~MemoryRegion() {
    reset();
}

MemoryRegion::MemoryRegion(MemoryRegion &&other) noexcept
    : endpoint_(std::exchange(other.endpoint_, nullptr)),
      buffer_(std::exchange(other.buffer_, nullptr)),
      info_(std::exchange(other.info_, {})),
      handle_(std::exchange(other.handle_, nullptr)) {}

MemoryRegion &MemoryRegion::operator=(MemoryRegion &&other) noexcept {
    if (this != &other) {
        reset();
        endpoint_ = std::exchange(other.endpoint_, nullptr);
        buffer_ = std::exchange(other.buffer_, nullptr);
        info_ = std::exchange(other.info_, {});
        handle_ = std::exchange(other.handle_, nullptr);
    }
    return *this;
}

void MemoryRegion::reset() noexcept {
    if (endpoint_ != nullptr && handle_ != nullptr)
        (void)endpoint_->deregister(handle_);
    endpoint_ = nullptr;
    buffer_ = nullptr;
    info_ = {};
    handle_ = nullptr;
}

std::uint64_t MemoryRegion::address() const noexcept {
    return reinterpret_cast<std::uint64_t>(info_.address);
}

std::uint32_t MemoryRegion::local_key() const noexcept {
    return info_.local_key;
}

std::uint32_t MemoryRegion::remote_key() const noexcept {
    return info_.remote_key;
}

std::uint64_t MemoryRegion::length() const noexcept {
    return info_.size;
}

bool MemoryRegion::belongs_to(const Endpoint *endpoint) const noexcept {
    return endpoint_ == endpoint && handle_ != nullptr;
}

QueuePair::QueuePair(Endpoint *endpoint, const QueuePairConfig &config, NpuBackend backend)
    : endpoint_(endpoint), config_(config), backend_(backend) {}

QueuePair::~QueuePair() {
    reset();
}

QueuePair::QueuePair(QueuePair &&other) noexcept
    : endpoint_(std::exchange(other.endpoint_, nullptr)),
      config_(std::move(other.config_)),
      backend_(other.backend_),
      handle_(std::exchange(other.handle_, nullptr)),
      local_attributes_(std::exchange(other.local_attributes_, {})),
      ai_qp_info_(std::exchange(other.ai_qp_info_, {})),
      send_wr_ids_(std::move(other.send_wr_ids_)),
      receive_wr_ids_(std::move(other.receive_wr_ids_)),
      posted_send_sge_(std::exchange(other.posted_send_sge_, {})),
      connected_(std::exchange(other.connected_, false)) {}

QueuePair &QueuePair::operator=(QueuePair &&other) noexcept {
    if (this != &other) {
        reset();
        endpoint_ = std::exchange(other.endpoint_, nullptr);
        config_ = std::move(other.config_);
        backend_ = other.backend_;
        handle_ = std::exchange(other.handle_, nullptr);
        local_attributes_ = std::exchange(other.local_attributes_, {});
        ai_qp_info_ = std::exchange(other.ai_qp_info_, {});
        send_wr_ids_ = std::move(other.send_wr_ids_);
        receive_wr_ids_ = std::move(other.receive_wr_ids_);
        posted_send_sge_ = std::exchange(other.posted_send_sge_, {});
        connected_ = std::exchange(other.connected_, false);
    }
    return *this;
}

Result<NdsRaTypicalQp> QueuePair::build_typical_qp(const NdsRaQpAttr &attributes, std::uint32_t traffic_class,
                                                   std::uint32_t service_level, std::uint32_t retry_count,
                                                   std::uint32_t retry_timeout) const {
    if (!is_valid_qp_number(attributes.qpn) || !is_valid_psn(attributes.psn))
        return Error{ErrorCode::kInvalidArgument, "invalid QP number or PSN"};
    NdsRaTypicalQp result{};
    result.qpn = attributes.qpn;
    result.psn = attributes.psn;
    result.gid_index = attributes.gid_index;
    std::memcpy(result.gid, attributes.gid, sizeof(result.gid));
    result.traffic_class = traffic_class;
    result.service_level = service_level;
    result.retry_count = retry_count;
    result.retry_timeout = retry_timeout;
    return result;
}

Result<void> QueuePair::initialize() {
    if (endpoint_ == nullptr || !endpoint_->opened())
        return Error{ErrorCode::kInvalidArgument, "QP creation requires an open endpoint"};
    const auto is_power_of_two = [](std::uint32_t value) { return value >= 2U && (value & (value - 1U)) == 0U; };
    if (config_.port_num == 0U || config_.path_mtu == 0U || !is_power_of_two(config_.send_queue_depth) ||
        !is_power_of_two(config_.receive_queue_depth)) {
        return Error{ErrorCode::kInvalidArgument,
                     "QP creation requires valid transport settings and power-of-two queue depths"};
    }
    if (backend_ != NpuBackend::Ra && backend_ != NpuBackend::Aicpu && backend_ != NpuBackend::Aiv) {
        return Error{ErrorCode::kInvalidArgument, "QP backend mode is invalid"};
    }
    if (backend_ == NpuBackend::Aicpu && config_.ai_qp_mode >= 0 && config_.ai_qp_mode != NDS_RA_QP_MODE_NORMAL) {
        return Error{ErrorCode::kInvalidArgument,
                     "AICPU backend requires a NORMAL AI QP so the provider owns Send doorbells"};
    }

    auto *api = &endpoint_->api_;
    if (api->ra_qp_destroy == nullptr || api->ra_get_qp_attr == nullptr || api->ra_typical_qp_modify == nullptr ||
        (backend_ == NpuBackend::Ra && api->ra_typical_qp_create == nullptr) ||
        ((backend_ == NpuBackend::Aicpu || backend_ == NpuBackend::Aiv) &&
         (api->ra_ai_qp_create == nullptr || api->ra_set_qp_attr_qos == nullptr ||
          api->ra_set_qp_attr_timeout == nullptr || api->ra_set_qp_attr_retry_count == nullptr))) {
        return Error{ErrorCode::kRa, "RA API is missing a required QP operation"};
    }

    int result{};
    if (config_.ai_qp_mode < 0)
        config_.ai_qp_mode = backend_ == NpuBackend::Aicpu ? NDS_RA_QP_MODE_NORMAL : NDS_RA_QP_MODE_OPBASE_EXT;
    if (backend_ == NpuBackend::Ra) {
        NdsRaTypicalQp initial_qp{};
        result = api->ra_typical_qp_create(endpoint_->rdev_handle_, NDS_RA_QP_FLAG_RC, NDS_RA_QP_MODE_OPBASE,
                                           &initial_qp, &handle_);
        if (result != 0 || handle_ == nullptr) {
            reset();
            return Error{ErrorCode::kRa, "RaTypicalQpCreate failed: " + std::to_string(result)};
        }
    } else {
        NdsRaQpExtAttrs attributes{};
        attributes.qp_mode = config_.ai_qp_mode;
        attributes.cq_attr.send_cq_depth = static_cast<int>(config_.send_queue_depth);
        attributes.cq_attr.recv_cq_depth = static_cast<int>(config_.receive_queue_depth);
        attributes.qp_attr.cap.max_send_wr = config_.send_queue_depth;
        attributes.qp_attr.cap.max_recv_wr = config_.receive_queue_depth;
        attributes.qp_attr.cap.max_send_sge = 1;
        attributes.qp_attr.cap.max_recv_sge = 1;
        attributes.qp_attr.cap.max_inline_data = 32;
        attributes.qp_attr.qp_type = NDS_RA_QP_TYPE_RC;
        attributes.version = NDS_RA_QP_CREATE_WITH_ATTR_VERSION;
        attributes.data_plane_flag = (config_.control_flags & QueuePairCallerPollsCq) != 0U
                                         ? static_cast<std::uint32_t>(NDS_RA_AI_CALLER_POLLS_CQ)
                                         : 0U;
        result = api->ra_ai_qp_create(endpoint_->rdev_handle_, &attributes, &ai_qp_info_, &handle_);
        if (result != 0 || handle_ == nullptr || ai_qp_info_.ai_qp_address == 0U) {
            reset();
            return Error{ErrorCode::kRa, "RaAiQpCreate failed: " + std::to_string(result)};
        }
        NdsRaQosAttr qos{};
        qos.traffic_class = static_cast<std::uint8_t>(config_.traffic_class);
        qos.service_level = static_cast<std::uint8_t>(config_.service_level);
        std::uint32_t timeout = config_.retry_timeout;
        std::uint32_t retry_count = config_.retry_count;
        if ((result = api->ra_set_qp_attr_qos(handle_, &qos)) != 0 ||
            (result = api->ra_set_qp_attr_timeout(handle_, &timeout)) != 0 ||
            (result = api->ra_set_qp_attr_retry_count(handle_, &retry_count)) != 0) {
            reset();
            return Error{ErrorCode::kRa, "setting AI QP attributes failed: " + std::to_string(result)};
        }

        if (backend_ == NpuBackend::Aiv) {
            // AIV writes WQEs and reads CQEs directly. Unlike the provider
            // paths, it therefore needs an NDS-owned slot-to-WR-ID mapping.
            auto send_wr_ids = endpoint_->runtime_->allocate(config_.send_queue_depth * sizeof(std::uint64_t));
            if (!send_wr_ids.ok()) {
                reset();
                return send_wr_ids.error();
            }
            auto receive_wr_ids = endpoint_->runtime_->allocate(config_.receive_queue_depth * sizeof(std::uint64_t));
            if (!receive_wr_ids.ok()) {
                reset();
                return receive_wr_ids.error();
            }
            send_wr_ids_ = std::move(send_wr_ids.value());
            receive_wr_ids_ = std::move(receive_wr_ids.value());
        }
    }
    result = api->ra_get_qp_attr(handle_, &local_attributes_);
    if (result != 0 || !is_valid_qp_number(local_attributes_.qpn) || !is_valid_psn(local_attributes_.psn)) {
        reset();
        return Error{ErrorCode::kRa, "RaGetQpAttr failed or returned invalid attributes: " + std::to_string(result)};
    }
    return {};
}

Result<nds::transport::QpInfo> QueuePair::local_qp_info() const {
    if (!created())
        return Error{ErrorCode::kInvalidArgument, "QP has not been created"};
    nds::transport::QpInfo info{};
    info.qp_num = local_attributes_.qpn;
    info.psn = local_attributes_.psn;
    info.port_num = config_.port_num;
    info.gid_index = static_cast<std::uint16_t>(local_attributes_.gid_index);
    info.path_mtu = config_.path_mtu;
    info.traffic_class = config_.traffic_class;
    info.service_level = config_.service_level;
    info.retry_count = config_.retry_count;
    info.retry_timeout = config_.retry_timeout;
    std::memcpy(info.gid, local_attributes_.gid, sizeof(info.gid));
    return info;
}

Result<void> QueuePair::connect(const nds::transport::QpInfo &peer) {
    if (!created() || connected_)
        return Error{ErrorCode::kInvalidArgument, "QP must be created and disconnected before connect"};
    if (peer.qp_num == 0U || peer.qp_num > kQpnMask || peer.psn > kPsnMask || peer.port_num == 0U ||
        peer.path_mtu == 0U) {
        return Error{ErrorCode::kInvalidArgument, "peer endpoint has invalid QP metadata"};
    }
    auto local = build_typical_qp(local_attributes_, config_.traffic_class, config_.service_level, config_.retry_count,
                                  config_.retry_timeout);
    if (!local)
        return Error{local.error()};
    NdsRaQpAttr peer_attributes{};
    peer_attributes.qpn = peer.qp_num;
    peer_attributes.psn = peer.psn;
    peer_attributes.gid_index = peer.gid_index;
    std::memcpy(peer_attributes.gid, peer.gid, sizeof(peer_attributes.gid));
    auto remote =
        build_typical_qp(peer_attributes, peer.traffic_class, peer.service_level, peer.retry_count, peer.retry_timeout);
    if (!remote)
        return Error{remote.error()};
    const int result = endpoint_->api_.ra_typical_qp_modify(handle_, &*local, &*remote);
    if (result != 0)
        return Error{ErrorCode::kRa, "RaTypicalQpModify failed: " + std::to_string(result)};
    connected_ = true;
    return {};
}

Result<int> QueuePair::query_port_status() {
    if (!created() || endpoint_->api_.ra_rdev_get_port_status == nullptr)
        return Error{ErrorCode::kInvalidArgument, "port-status query requires a created QP"};
    int status{};
    const int result = endpoint_->api_.ra_rdev_get_port_status(endpoint_->rdev_handle_, &status);
    if (result != 0 || status < NDS_RA_PORT_STATUS_DOWN || status > NDS_RA_PORT_STATUS_ACTIVE)
        return Error{ErrorCode::kRa, "RaRdevGetPortStatus failed or returned invalid status"};
    return status;
}

Result<int> QueuePair::query_support_lite() {
    if (!created() || endpoint_->api_.ra_rdev_get_support_lite == nullptr)
        return Error{ErrorCode::kInvalidArgument, "RDMA-lite query requires a created QP"};
    int support_lite{};
    const int result = endpoint_->api_.ra_rdev_get_support_lite(endpoint_->rdev_handle_, &support_lite);
    if (result != 0 || support_lite < NDS_RA_LITE_NOT_SUPPORTED || support_lite > NDS_RA_LITE_ALIGN_2M)
        return Error{ErrorCode::kRa, "RaRdevGetSupportLite failed or returned invalid status"};
    return support_lite;
}

Result<int> QueuePair::query_status() {
    if (!created() || endpoint_->api_.ra_get_qp_status == nullptr)
        return Error{ErrorCode::kInvalidArgument, "QP-status query requires a created QP"};
    int status{};
    const int result = endpoint_->api_.ra_get_qp_status(handle_, &status);
    if (result != 0 || status < NDS_RA_QP_STATUS_NOT_CONNECTED || status > NDS_RA_QP_STATUS_CONNECTING)
        return Error{ErrorCode::kRa, "RaGetQpStatus failed or returned invalid status"};
    return status;
}

Result<std::vector<NdsRaCqeError>> QueuePair::query_cqe_errors() {
    if (!created() || endpoint_->api_.ra_rdev_get_cqe_error_list == nullptr)
        return Error{ErrorCode::kInvalidArgument, "CQE-error query requires a created QP"};
    std::vector<NdsRaCqeError> errors(kMaxCqeErrors);
    unsigned int count = static_cast<unsigned int>(errors.size());
    const int result = endpoint_->api_.ra_rdev_get_cqe_error_list(endpoint_->rdev_handle_, errors.data(), &count);
    if (result != 0 || count > errors.size())
        return Error{ErrorCode::kRa, "RaRdevGetCqeErrInfoList failed or exceeded error capacity"};
    errors.resize(count);
    return errors;
}

void QueuePair::reset() noexcept {
    if (endpoint_ != nullptr && handle_ != nullptr && endpoint_->api_.ra_qp_destroy != nullptr)
        (void)endpoint_->api_.ra_qp_destroy(handle_);
    endpoint_ = nullptr;
    handle_ = nullptr;
    local_attributes_ = {};
    ai_qp_info_ = {};
    posted_send_sge_ = {};
    connected_ = false;
}

bool QueuePair::created() const noexcept {
    return endpoint_ != nullptr && handle_ != nullptr;
}

bool QueuePair::connected() const noexcept {
    return connected_;
}

const NdsRaQpAttr &QueuePair::local_attributes() const noexcept {
    return local_attributes_;
}

NpuBackend QueuePair::backend_mode() const noexcept {
    return backend_;
}

NdsRaApi *QueuePair::ra_api() const noexcept {
    return endpoint_ == nullptr ? nullptr : &endpoint_->api_;
}

void *QueuePair::handle() const noexcept {
    return handle_;
}

NdsRaSge *QueuePair::posted_send_sge() noexcept {
    return &posted_send_sge_;
}

Endpoint::~Endpoint() {
    reset();
}

Result<void> Endpoint::open(Runtime *runtime, const EndpointConfig &config) {
    if (opened() || runtime == nullptr || !runtime->initialized() || config.ra_library.empty()) {
        return Error{ErrorCode::kInvalidArgument, "endpoint open requires one runtime and RA library"};
    }
    runtime_ = runtime;
    config_ = config;
    const auto logical_device = static_cast<std::int32_t>(runtime_->config().logical_device_id);
    std::int32_t physical_device = -1;
    if (aclrtGetPhyDevIdByLogicDevId(logical_device, &physical_device) != ACL_SUCCESS || physical_device < 0) {
        reset();
        return Error{ErrorCode::kRuntime,
                     "CANN cannot map logical device " + std::to_string(logical_device) + " to a physical device"};
    }
    physical_device_id_ = static_cast<std::uint32_t>(physical_device);
    const auto api = nds_ra_open(config_.ra_library);
    if (!api) {
        reset();
        return Error{api.error()};
    }
    api_ = *api;
    NdsRaInitConfig init_config{};
    init_config.phy_id = physical_device_id_;
    init_config.nic_position = NDS_RA_NETWORK_OFFLINE;
    init_config.hdc_type = config_.hdc_type;
    init_config.enable_hdc_async = false;
    int result = api_.ra_init(&init_config);
    if (result != 0) {
        reset();
        return Error{ErrorCode::kRa, "RaInit failed: " + std::to_string(result)};
    }
    ra_initialized_ = true;

    NdsRaRdev rdev{};
    rdev.phy_id = physical_device_id_;
    rdev.family = AF_INET;
    const auto ipv4 = dsmi_ipv4(physical_device_id_);
    if (!ipv4) {
        reset();
        return Error{ipv4.error()};
    }
    if (inet_pton(AF_INET, ipv4->c_str(), &rdev.local_ip.ipv4) != 1) {
        reset();
        return Error{ErrorCode::kInvalidArgument, "endpoint local IPv4 address is invalid"};
    }
    NdsRaRdevInitInfo rdev_init{};
    rdev_init.mode = NDS_RA_NETWORK_OFFLINE;
    rdev_init.notify_type = NDS_RA_NOTIFY;
    rdev_init.disabled_lite_thread = false;
    result = api_.ra_rdev_init_v2(rdev_init, rdev, &rdev_handle_);
    if (result != 0 || rdev_handle_ == nullptr) {
        reset();
        return Error{ErrorCode::kRa, "RaRdevInitV2 failed for " + *ipv4 + ": " + std::to_string(result)};
    }
    return {};
}

Result<QueuePair> Endpoint::create_qp(const QueuePairConfig &config, NpuBackend backend) {
    QueuePair qp(this, config, backend);
    if (const auto initialized = qp.initialize(); !initialized)
        return Error{initialized.error()};
    return qp;
}

Result<MemoryRegion> Endpoint::reg_mr(const MemoryBuffer &buffer, MemoryAccess access) {
    if (!opened() || buffer.rdma_data() == nullptr || buffer.size() == 0U || api_.ra_register_mr == nullptr)
        return Error{ErrorCode::kInvalidArgument, "MR registration requires an open endpoint and buffer"};
    MemoryRegion region;
    region.info_.address = buffer.rdma_data();
    region.info_.size = buffer.size();
    region.info_.access = static_cast<int>(access);
    const int result = api_.ra_register_mr(rdev_handle_, &region.info_, &region.handle_);
    if (result != 0 || region.handle_ == nullptr || region.info_.local_key == 0U || region.info_.remote_key == 0U)
        return Error{ErrorCode::kRa, "RaRegisterMr failed or returned invalid keys: " + std::to_string(result)};
    region.endpoint_ = this;
    region.buffer_ = &buffer;
    return region;
}

Result<void> Endpoint::deregister(void *handle) {
    if (!opened() || handle == nullptr || api_.ra_deregister_mr == nullptr)
        return Error{ErrorCode::kInvalidArgument, "MR deregistration requires an open endpoint and handle"};
    const int result = api_.ra_deregister_mr(rdev_handle_, handle);
    if (result != 0)
        return Error{ErrorCode::kRa, "RaDeregisterMr failed: " + std::to_string(result)};
    return {};
}

bool Endpoint::opened() const noexcept {
    return runtime_ != nullptr && rdev_handle_ != nullptr;
}

void Endpoint::reset() noexcept {
    if (rdev_handle_ != nullptr && api_.ra_rdev_deinit != nullptr)
        (void)api_.ra_rdev_deinit(rdev_handle_, NDS_RA_NOTIFY);
    rdev_handle_ = nullptr;
    if (ra_initialized_) {
        NdsRaInitConfig init_config{};
        init_config.phy_id = physical_device_id_;
        init_config.nic_position = NDS_RA_NETWORK_OFFLINE;
        init_config.hdc_type = config_.hdc_type;
        init_config.enable_hdc_async = false;
        (void)api_.ra_deinit(&init_config);
        ra_initialized_ = false;
    }
    nds_ra_close(&api_);
    runtime_ = nullptr;
    config_ = {};
    physical_device_id_ = {};
}

}  // namespace nds::client
