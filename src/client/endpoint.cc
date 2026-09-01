#include "endpoint.hh"

#include "backends/backend_mode.hh"
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

Result<Endpoint> Runtime::create_endpoint(const EndpointConfig &config) {
    if (!initialized())
        return Error{ErrorCode::kInvalidArgument, "endpoint creation requires an open runtime"};
    Endpoint endpoint;
    NDS_RETURN_IF_ERROR(endpoint.open(this, config));
    return endpoint;
}

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

QueuePair::QueuePair(Endpoint *endpoint, const QueuePairConfig &config, BackendMode mode)
    : endpoint_(endpoint), config_(config), mode_(mode) {}

QueuePair::~QueuePair() {
    reset();
}

QueuePair::QueuePair(QueuePair &&other) noexcept
    : endpoint_(std::exchange(other.endpoint_, nullptr)),
      config_(std::move(other.config_)),
      mode_(other.mode_),
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
        mode_ = other.mode_;
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

Result<Libra::TypicalQp> QueuePair::build_typical_qp(const Libra::QpAttr &attributes, std::uint32_t traffic_class,
                                                     std::uint32_t service_level, std::uint32_t retry_count,
                                                     std::uint32_t retry_timeout) const {
    if (!is_valid_qp_number(attributes.qpn) || !is_valid_psn(attributes.psn))
        return Error{ErrorCode::kInvalidArgument, "invalid QP number or PSN"};
    Libra::TypicalQp result{};
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
    if (mode_ != BackendMode::Ra && mode_ != BackendMode::Aicpu && mode_ != BackendMode::Aiv) {
        return Error{ErrorCode::kInvalidArgument, "QP backend mode is invalid"};
    }
    if (mode_ == BackendMode::Aicpu && config_.ai_qp_mode >= 0 && config_.ai_qp_mode != Libra::QP_MODE_NORMAL) {
        return Error{ErrorCode::kInvalidArgument,
                     "AICPU backend requires a NORMAL AI QP so the provider owns Send doorbells"};
    }

    Libra *const libra = &endpoint_->libra_;
    if (libra->qp_destroy == nullptr || libra->get_qp_attr == nullptr || libra->typical_qp_modify == nullptr ||
        (mode_ == BackendMode::Ra && libra->typical_qp_create == nullptr) ||
        ((mode_ == BackendMode::Aicpu || mode_ == BackendMode::Aiv) &&
         (libra->ai_qp_create == nullptr || libra->set_qp_attr_qos == nullptr ||
          libra->set_qp_attr_timeout == nullptr || libra->set_qp_attr_retry_count == nullptr))) {
        return Error{ErrorCode::kRa, "RA API is missing a required QP operation"};
    }

    int result{};
    if (config_.ai_qp_mode < 0)
        config_.ai_qp_mode = mode_ == BackendMode::Aicpu ? Libra::QP_MODE_NORMAL : Libra::QP_MODE_OPBASE_EXT;
    if (mode_ == BackendMode::Ra) {
        Libra::TypicalQp initial_qp{};
        result = libra->typical_qp_create(endpoint_->rdev_handle_, Libra::QP_FLAG_RC, Libra::QP_MODE_OPBASE,
                                          &initial_qp, &handle_);
        if (result != 0 || handle_ == nullptr) {
            reset();
            return Error{ErrorCode::kRa, "RaTypicalQpCreate failed: " + std::to_string(result)};
        }
    } else {
        Libra::QpExtAttrs attributes{};
        attributes.qp_mode = config_.ai_qp_mode;
        attributes.cq_attr.send_cq_depth = static_cast<int>(config_.send_queue_depth);
        attributes.cq_attr.recv_cq_depth = static_cast<int>(config_.receive_queue_depth);
        attributes.qp_attr.cap.max_send_wr = config_.send_queue_depth;
        attributes.qp_attr.cap.max_recv_wr = config_.receive_queue_depth;
        attributes.qp_attr.cap.max_send_sge = 1;
        attributes.qp_attr.cap.max_recv_sge = 1;
        attributes.qp_attr.cap.max_inline_data = 32;
        attributes.qp_attr.qp_type = Libra::QP_TYPE_RC;
        attributes.version = Libra::QP_CREATE_WITH_ATTR_VERSION;
        attributes.data_plane_flag = (config_.control_flags & QueuePairCallerPollsCq) != 0U
                                         ? static_cast<std::uint32_t>(Libra::AI_CALLER_POLLS_CQ)
                                         : 0U;
        result = libra->ai_qp_create(endpoint_->rdev_handle_, &attributes, &ai_qp_info_, &handle_);
        if (result != 0 || handle_ == nullptr || ai_qp_info_.ai_qp_address == 0U) {
            reset();
            return Error{ErrorCode::kRa, "RaAiQpCreate failed: " + std::to_string(result)};
        }
        Libra::QosAttr qos{};
        qos.traffic_class = static_cast<std::uint8_t>(config_.traffic_class);
        qos.service_level = static_cast<std::uint8_t>(config_.service_level);
        std::uint32_t timeout = config_.retry_timeout;
        std::uint32_t retry_count = config_.retry_count;
        if ((result = libra->set_qp_attr_qos(handle_, &qos)) != 0 ||
            (result = libra->set_qp_attr_timeout(handle_, &timeout)) != 0 ||
            (result = libra->set_qp_attr_retry_count(handle_, &retry_count)) != 0) {
            reset();
            return Error{ErrorCode::kRa, "setting AI QP attributes failed: " + std::to_string(result)};
        }

        if (mode_ == BackendMode::Aiv || mode_ == BackendMode::Aicpu) {
            // AI caller polling reads CQEs by queue slot. Keep the caller WR
            // identity in NDS-owned device memory for both AI implementations.
            auto send_wr_ids =
                endpoint_->runtime_->allocate(config_.send_queue_depth * sizeof(std::uint64_t), MemoryLocation::Device);
            if (!send_wr_ids.ok()) {
                reset();
                return send_wr_ids.error();
            }
            auto receive_wr_ids = endpoint_->runtime_->allocate(config_.receive_queue_depth * sizeof(std::uint64_t),
                                                                MemoryLocation::Device);
            if (!receive_wr_ids.ok()) {
                reset();
                return receive_wr_ids.error();
            }
            send_wr_ids_ = std::move(send_wr_ids.value());
            receive_wr_ids_ = std::move(receive_wr_ids.value());
        }
    }
    result = libra->get_qp_attr(handle_, &local_attributes_);
    if (result != 0 || !is_valid_qp_number(local_attributes_.qpn) || !is_valid_psn(local_attributes_.psn)) {
        reset();
        return Error{ErrorCode::kRa, "RaGetQpAttr failed or returned invalid attributes: " + std::to_string(result)};
    }
    return {};
}

Result<nds::QpInfo> QueuePair::local_qp_info() const {
    if (!created())
        return Error{ErrorCode::kInvalidArgument, "QP has not been created"};
    nds::QpInfo info{};
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

Result<void> QueuePair::connect(const nds::QpInfo &peer) {
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
    Libra::QpAttr peer_attributes{};
    peer_attributes.qpn = peer.qp_num;
    peer_attributes.psn = peer.psn;
    peer_attributes.gid_index = peer.gid_index;
    std::memcpy(peer_attributes.gid, peer.gid, sizeof(peer_attributes.gid));
    auto remote =
        build_typical_qp(peer_attributes, peer.traffic_class, peer.service_level, peer.retry_count, peer.retry_timeout);
    if (!remote)
        return Error{remote.error()};
    const int result = endpoint_->libra_.typical_qp_modify(handle_, &local.value(), &remote.value());
    if (result != 0)
        return Error{ErrorCode::kRa, "RaTypicalQpModify failed: " + std::to_string(result)};
    connected_ = true;
    return {};
}

Result<int> QueuePair::query_port_status() {
    if (!created() || endpoint_->libra_.rdev_get_port_status == nullptr)
        return Error{ErrorCode::kInvalidArgument, "port-status query requires a created QP"};
    int status{};
    const int result = endpoint_->libra_.rdev_get_port_status(endpoint_->rdev_handle_, &status);
    if (result != 0 || status < Libra::PORT_STATUS_DOWN || status > Libra::PORT_STATUS_ACTIVE)
        return Error{ErrorCode::kRa, "RaRdevGetPortStatus failed or returned invalid status"};
    return status;
}

Result<int> QueuePair::query_support_lite() {
    if (!created() || endpoint_->libra_.rdev_get_support_lite == nullptr)
        return Error{ErrorCode::kInvalidArgument, "RDMA-lite query requires a created QP"};
    int support_lite{};
    const int result = endpoint_->libra_.rdev_get_support_lite(endpoint_->rdev_handle_, &support_lite);
    if (result != 0 || support_lite < Libra::LITE_NOT_SUPPORTED || support_lite > Libra::LITE_ALIGN_2M)
        return Error{ErrorCode::kRa, "RaRdevGetSupportLite failed or returned invalid status"};
    return support_lite;
}

Result<int> QueuePair::query_status() {
    if (!created() || endpoint_->libra_.get_qp_status == nullptr)
        return Error{ErrorCode::kInvalidArgument, "QP-status query requires a created QP"};
    int status{};
    const int result = endpoint_->libra_.get_qp_status(handle_, &status);
    if (result != 0 || status < Libra::QP_STATUS_NOT_CONNECTED || status > Libra::QP_STATUS_CONNECTING)
        return Error{ErrorCode::kRa, "RaGetQpStatus failed or returned invalid status"};
    return status;
}

Result<std::vector<Libra::CqeError>> QueuePair::query_cqe_errors() {
    if (!created() || endpoint_->libra_.rdev_get_cqe_error_list == nullptr)
        return Error{ErrorCode::kInvalidArgument, "CQE-error query requires a created QP"};
    std::vector<Libra::CqeError> errors(kMaxCqeErrors);
    unsigned int count = static_cast<unsigned int>(errors.size());
    const int result = endpoint_->libra_.rdev_get_cqe_error_list(endpoint_->rdev_handle_, errors.data(), &count);
    if (result != 0 || count > errors.size())
        return Error{ErrorCode::kRa, "RaRdevGetCqeErrInfoList failed or exceeded error capacity"};
    errors.resize(count);
    return errors;
}

void QueuePair::reset() noexcept {
    if (endpoint_ != nullptr && handle_ != nullptr && endpoint_->libra_.qp_destroy != nullptr)
        (void)endpoint_->libra_.qp_destroy(handle_);
    endpoint_ = nullptr;
    handle_ = nullptr;
    local_attributes_ = {};
    ai_qp_info_ = {};
    posted_send_sge_ = {};
    connected_ = false;
}

Result<NdsDeviceQp> QueuePair::device_qp() const {
    if (!created())
        return Error{ErrorCode::kInvalidArgument, "device QP requires a created QP"};

    NdsDeviceQp descriptor{};
    descriptor.host_runtime_address = reinterpret_cast<std::uint64_t>(endpoint_->runtime_);
    descriptor.host_qp_address = reinterpret_cast<std::uint64_t>(this);
    if (mode_ == BackendMode::Ra)
        return descriptor;

    if (ai_qp_info_.ai_qp_address == 0U || ai_qp_info_.data_plane_info == nullptr)
        return Error{ErrorCode::kRa, "AI QP is missing provider metadata"};
    if ((mode_ == BackendMode::Aiv || mode_ == BackendMode::Aicpu) &&
        (send_wr_ids_.data() == nullptr || receive_wr_ids_.data() == nullptr))
        return Error{ErrorCode::kRuntime, "AI QP is missing private WR-ID storage"};

    const auto *source = reinterpret_cast<const Libra::AiDataPlaneInfo *>(ai_qp_info_.data_plane_info);
    if (source->send_wq.buffer_address == 0U || source->receive_wq.buffer_address == 0U)
        return Error{ErrorCode::kRa, "AI QP is missing SQ/RQ metadata"};

    const auto copy_wq = [](const Libra::AiDataPlaneWq &input, std::uint64_t wr_id_address, bool is_send) {
        return NdsDeviceWorkQueue{input.wqn,
                                  input.depth,
                                  input.wqebb_size,
                                  is_send ? NDS_DEVICE_DOORBELL_MMIO : NDS_DEVICE_DOORBELL_RECORD,
                                  input.buffer_address,
                                  input.head_address,
                                  input.tail_address,
                                  is_send ? input.doorbell_register_address : input.software_doorbell_address,
                                  wr_id_address};
    };
    const auto copy_cq = [](const Libra::AiDataPlaneCq &input) {
        return NdsDeviceCq{input.cqn,
                           input.depth,
                           input.cqe_size,
                           NDS_DEVICE_DOORBELL_RECORD,
                           input.buffer_address,
                           input.tail_address,
                           input.software_doorbell_address};
    };

    descriptor.flags = (config_.control_flags & QueuePairCallerPollsCq) != 0U
                           ? static_cast<std::uint32_t>(NDS_DEVICE_QP_CALLER_POLLS_CQ)
                           : 0U;
    descriptor.qp_mode = config_.ai_qp_mode;
    descriptor.service_level = config_.service_level;
    descriptor.provider_qp_address = ai_qp_info_.ai_qp_address;
    descriptor.provider_send_cq_address = ai_qp_info_.ai_scq_address;
    descriptor.provider_receive_cq_address = ai_qp_info_.ai_rcq_address;
    const std::uint64_t send_wr_ids = (mode_ == BackendMode::Aiv || mode_ == BackendMode::Aicpu)
                                          ? reinterpret_cast<std::uint64_t>(send_wr_ids_.data())
                                          : 0U;
    const std::uint64_t receive_wr_ids = (mode_ == BackendMode::Aiv || mode_ == BackendMode::Aicpu)
                                             ? reinterpret_cast<std::uint64_t>(receive_wr_ids_.data())
                                             : 0U;
    descriptor.send_queue = copy_wq(source->send_wq, send_wr_ids, true);
    descriptor.receive_queue = copy_wq(source->receive_wq, receive_wr_ids, false);
    descriptor.send_cq = copy_cq(source->send_cq);
    descriptor.receive_cq = copy_cq(source->receive_cq);
    return descriptor;
}

bool QueuePair::created() const noexcept {
    return endpoint_ != nullptr && handle_ != nullptr;
}

bool QueuePair::connected() const noexcept {
    return connected_;
}

const Libra::QpAttr &QueuePair::local_attributes() const noexcept {
    return local_attributes_;
}

BackendMode QueuePair::backend_mode() const noexcept {
    return mode_;
}

Libra *QueuePair::libra() const noexcept {
    return endpoint_ == nullptr ? nullptr : &endpoint_->libra_;
}

void *QueuePair::handle() const noexcept {
    return handle_;
}

Libra::Sge *QueuePair::posted_send_sge() noexcept {
    return &posted_send_sge_;
}

Endpoint::~Endpoint() {
    reset();
}

Endpoint::Endpoint(Endpoint &&other) noexcept
    : runtime_(std::exchange(other.runtime_, nullptr)),
      config_(std::move(other.config_)),
      physical_device_id_(std::exchange(other.physical_device_id_, 0U)),
      libra_(std::move(other.libra_)),
      rdev_handle_(std::exchange(other.rdev_handle_, nullptr)),
      ra_initialized_(std::exchange(other.ra_initialized_, false)) {}

Endpoint &Endpoint::operator=(Endpoint &&other) noexcept {
    if (this != &other) {
        reset();
        runtime_ = std::exchange(other.runtime_, nullptr);
        config_ = std::move(other.config_);
        physical_device_id_ = std::exchange(other.physical_device_id_, 0U);
        libra_ = std::move(other.libra_);
        rdev_handle_ = std::exchange(other.rdev_handle_, nullptr);
        ra_initialized_ = std::exchange(other.ra_initialized_, false);
    }
    return *this;
}

Result<void> Endpoint::open(Runtime *runtime, const EndpointConfig &config) {
    if (opened() || runtime == nullptr || !runtime->initialized()) {
        return Error{ErrorCode::kInvalidArgument, "endpoint open requires one initialized runtime"};
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
    Result<Libra> libra_result = Libra::open();
    if (!libra_result.ok()) {
        reset();
        return Error{libra_result.error()};
    }
    libra_ = std::move(libra_result).value();
    Libra::InitConfig init_config{};
    init_config.phy_id = physical_device_id_;
    init_config.nic_position = Libra::NETWORK_OFFLINE;
    init_config.hdc_type = config_.hdc_type;
    init_config.enable_hdc_async = false;
    int result = libra_.init(&init_config);
    if (result != 0) {
        reset();
        return Error{ErrorCode::kRa, "RaInit failed: " + std::to_string(result)};
    }
    ra_initialized_ = true;

    Libra::Rdev rdev{};
    rdev.phy_id = physical_device_id_;
    rdev.family = AF_INET;
    constexpr int kDsmiRocePort = 1;
    constexpr int kDsmiPortId = 0;
    Libdsmi::IpAddress address{};
    Libdsmi::IpAddress netmask{};
    address.type = Libdsmi::IP_ADDRESS_V4;
    netmask.type = Libdsmi::IP_ADDRESS_V4;
    const int dsmi_result = runtime_->libdsmi().get_device_ip_address(static_cast<int>(physical_device_id_),
                                                                      kDsmiRocePort, kDsmiPortId, &address, &netmask);
    if (dsmi_result != 0) {
        reset();
        return Error{ErrorCode::kRuntime, "dsmi_get_device_ip_address failed: " + std::to_string(dsmi_result)};
    }
    std::memcpy(&rdev.local_ip.ipv4.s_addr, address.address.ip4, sizeof(rdev.local_ip.ipv4.s_addr));
    Libra::RdevInitInfo rdev_init{};
    rdev_init.mode = Libra::NETWORK_OFFLINE;
    rdev_init.notify_type = Libra::NOTIFY;
    rdev_init.disabled_lite_thread = false;
    result = libra_.rdev_init_v2(rdev_init, rdev, &rdev_handle_);
    if (result != 0 || rdev_handle_ == nullptr) {
        reset();
        return Error{ErrorCode::kRa, "RaRdevInitV2 failed: " + std::to_string(result)};
    }
    return {};
}

Result<QueuePair> Endpoint::create_qp(const QueuePairConfig &config, BackendMode backend) {
    QueuePair qp(this, config, backend);
    if (const auto initialized = qp.initialize(); !initialized)
        return Error{initialized.error()};
    return qp;
}

Result<MemoryRegion> Endpoint::reg_mr(const MemoryBuffer &buffer, MemoryAccess access) {
    if (!opened() || buffer.rdma_data() == nullptr || buffer.size() == 0U || libra_.register_mr == nullptr)
        return Error{ErrorCode::kInvalidArgument, "MR registration requires an open endpoint and buffer"};
    MemoryRegion region;
    region.info_.address = buffer.rdma_data();
    region.info_.size = buffer.size();
    region.info_.access = static_cast<int>(access);
    const int result = libra_.register_mr(rdev_handle_, &region.info_, &region.handle_);
    if (result != 0 || region.handle_ == nullptr || region.info_.local_key == 0U || region.info_.remote_key == 0U)
        return Error{ErrorCode::kRa, "RaRegisterMr failed or returned invalid keys: " + std::to_string(result)};
    region.endpoint_ = this;
    region.buffer_ = &buffer;
    return region;
}

Result<void> Endpoint::deregister(void *handle) {
    if (!opened() || handle == nullptr || libra_.deregister_mr == nullptr)
        return Error{ErrorCode::kInvalidArgument, "MR deregistration requires an open endpoint and handle"};
    const int result = libra_.deregister_mr(rdev_handle_, handle);
    if (result != 0)
        return Error{ErrorCode::kRa, "RaDeregisterMr failed: " + std::to_string(result)};
    return {};
}

bool Endpoint::opened() const noexcept {
    return runtime_ != nullptr && rdev_handle_ != nullptr;
}

void Endpoint::reset() noexcept {
    if (rdev_handle_ != nullptr && libra_.rdev_deinit != nullptr)
        (void)libra_.rdev_deinit(rdev_handle_, Libra::NOTIFY);
    rdev_handle_ = nullptr;
    if (ra_initialized_) {
        Libra::InitConfig init_config{};
        init_config.phy_id = physical_device_id_;
        init_config.nic_position = Libra::NETWORK_OFFLINE;
        init_config.hdc_type = config_.hdc_type;
        init_config.enable_hdc_async = false;
        (void)libra_.deinit(&init_config);
        ra_initialized_ = false;
    }
    runtime_ = nullptr;
    config_ = {};
    physical_device_id_ = {};
}

}  // namespace nds::client
