#include "nds/npu_ra_qp.hh"

#include <arpa/inet.h>

#include <cstring>
#include <limits>
#include <utility>

namespace nds {
namespace {

constexpr std::uint32_t kQpnMask = 0x00ffffffU;
constexpr std::uint32_t kPsnMask = 0x00ffffffU;

bool is_valid_qp_number(std::uint32_t value) {
    return value != 0U && value <= kQpnMask;
}

bool is_valid_psn(std::uint32_t value) {
    return value <= kPsnMask;
}

}  // namespace

NpuRaQp::~NpuRaQp() {
    reset();
}

void NpuRaQp::set_error(std::string message) {
    error_ = std::move(message);
}

bool NpuRaQp::build_typical_qp(const nds_ra_qp_attr &attributes, std::uint32_t traffic_class,
                               std::uint32_t service_level, std::uint32_t retry_count, std::uint32_t retry_timeout,
                               nds_ra_typical_qp *out) const {
    if (out == nullptr || !is_valid_qp_number(attributes.qpn) || !is_valid_psn(attributes.psn)) {
        return false;
    }

    *out = {};
    out->qpn = attributes.qpn;
    out->psn = attributes.psn;
    out->gid_index = attributes.gid_index;
    std::memcpy(out->gid, attributes.gid, sizeof(out->gid));
    out->traffic_class = traffic_class;
    out->service_level = service_level;
    out->retry_count = retry_count;
    out->retry_timeout = retry_timeout;
    return true;
}

bool NpuRaQp::create(nds_ra_api *api, const NpuRaQpConfig &config, NpuExecutionMode execution) {
    nds_ra_rdev rdev{};
    nds_ra_rdev_init_info rdev_init{};
    nds_ra_typical_qp initial_qp{};
    nds_ra_qp_ext_attrs ai_attrs{};
    int result;

    if (api == nullptr) {
        set_error("NPU RA QP requires RA API storage");
        return false;
    }
    if (created()) {
        set_error("NPU RA QP is already created");
        return false;
    }
    const auto is_power_of_two = [](std::uint32_t value) {
        return value >= 2U && (value & (value - 1U)) == 0U;
    };
    if (config.local_ipv4.empty() || config.port_num == 0U || config.path_mtu == 0U ||
        !is_power_of_two(config.send_queue_depth) || !is_power_of_two(config.receive_queue_depth)) {
        set_error("NPU RA QP requires valid network settings and power-of-two queue depths of at least two");
        return false;
    }
    if (execution != NpuExecutionMode::Ra && execution != NpuExecutionMode::Aicpu &&
        execution != NpuExecutionMode::Aiv) {
        set_error("NPU RA QP execution mode is invalid");
        return false;
    }
    if (execution == NpuExecutionMode::Aicpu && config.ai_qp_mode >= 0 && config.ai_qp_mode != NDS_RA_QP_MODE_NORMAL) {
        set_error("AICPU execution requires a NORMAL AI QP so the provider owns Send doorbell submission");
        return false;
    }
    if (api->ra_rdev_init_v2 == nullptr || api->ra_rdev_deinit == nullptr || api->ra_qp_destroy == nullptr ||
        api->ra_get_qp_attr == nullptr || api->ra_typical_qp_modify == nullptr ||
        (execution == NpuExecutionMode::Ra && api->ra_typical_qp_create == nullptr) ||
        ((execution == NpuExecutionMode::Aicpu || execution == NpuExecutionMode::Aiv) &&
         (api->ra_ai_qp_create == nullptr || api->ra_set_qp_attr_qos == nullptr ||
          api->ra_set_qp_attr_timeout == nullptr || api->ra_set_qp_attr_retry_count == nullptr))) {
        set_error("RA API is missing a required rdev/QP/query operation for the selected execution mode");
        return false;
    }

    rdev.phy_id = config.physical_device_id;
    rdev.family = AF_INET;
    if (inet_pton(AF_INET, config.local_ipv4.c_str(), &rdev.local_ip.ipv4) != 1) {
        set_error("NPU RA QP local IPv4 address is invalid: " + config.local_ipv4);
        return false;
    }

    api_ = api;
    config_ = config;
    if (config_.ai_qp_mode < 0) {
        config_.ai_qp_mode = execution == NpuExecutionMode::Aicpu ?
                                 NDS_RA_QP_MODE_NORMAL : NDS_RA_QP_MODE_OPBASE_EXT;
    }
    execution_ = execution;
    rdev_init.mode = NDS_RA_NETWORK_OFFLINE;
    rdev_init.notify_type = NDS_RA_NOTIFY;
    rdev_init.enabled_910a_lite = false;
    /* HCOMM's device-RoCE rdev setup leaves the provider's lite context active. */
    rdev_init.disabled_lite_thread = false;
    rdev_init.enabled_2mb_lite = false;
    result = api_->ra_rdev_init_v2(rdev_init, rdev, &rdev_handle_);
    if (result != 0 || rdev_handle_ == nullptr) {
        set_error("RaRdevInitV2(disabledLiteThread=false) failed: " + std::to_string(result));
        reset();
        return false;
    }
    if (execution_ == NpuExecutionMode::Ra) {
        result = api_->ra_typical_qp_create(rdev_handle_, NDS_RA_QP_FLAG_RC, NDS_RA_QP_MODE_OPBASE, &initial_qp,
                                            &qp_handle_);
        if (result != 0 || qp_handle_ == nullptr) {
            set_error("RaTypicalQpCreate failed: " + std::to_string(result));
            reset();
            return false;
        }
    } else {
        /* Normal AI mode lets CP1's provider post ring sq.db_reg directly. AIV needs OPBASE_EXT metadata. */
        ai_attrs.qp_mode = config_.ai_qp_mode;
        ai_attrs.cq_attr.send_cq_depth = static_cast<int>(config_.send_queue_depth);
        ai_attrs.cq_attr.recv_cq_depth = static_cast<int>(config_.receive_queue_depth);
        ai_attrs.qp_attr.cap.max_send_wr = config_.send_queue_depth;
        ai_attrs.qp_attr.cap.max_recv_wr = config_.receive_queue_depth;
        ai_attrs.qp_attr.cap.max_send_sge = 1;
        ai_attrs.qp_attr.cap.max_recv_sge = 1;
        ai_attrs.qp_attr.cap.max_inline_data = 32;
        ai_attrs.qp_attr.qp_type = NDS_RA_QP_TYPE_RC;
        ai_attrs.version = NDS_RA_QP_CREATE_WITH_ATTR_VERSION;
        ai_attrs.data_plane_flag = (config_.control_flags & NpuRaQpCallerPollsCq) != 0U ?
                                       static_cast<std::uint32_t>(NDS_RA_AI_CALLER_POLLS_CQ) : 0U;
        result = api_->ra_ai_qp_create(rdev_handle_, &ai_attrs, &ai_qp_info_, &qp_handle_);
        if (result != 0 || qp_handle_ == nullptr || ai_qp_info_.ai_qp_address == 0U) {
            set_error("RaAiQpCreate(AI execution mode) failed: " + std::to_string(result));
            reset();
            return false;
        }
        /* HCOMM's CreateAiQp applies these local AI-QP attributes after creation. */
        nds_ra_qos_attr qos{};
        uint32_t timeout = config_.retry_timeout;
        uint32_t retry_count = config_.retry_count;
        qos.traffic_class = static_cast<uint8_t>(config_.traffic_class);
        qos.service_level = static_cast<uint8_t>(config_.service_level);
        result = api_->ra_set_qp_attr_qos(qp_handle_, &qos);
        if (result != 0) {
            set_error("RaSetQpAttrQos(AI QP) failed: " + std::to_string(result));
            reset();
            return false;
        }
        result = api_->ra_set_qp_attr_timeout(qp_handle_, &timeout);
        if (result != 0) {
            set_error("RaSetQpAttrTimeout(AI QP) failed: " + std::to_string(result));
            reset();
            return false;
        }
        result = api_->ra_set_qp_attr_retry_count(qp_handle_, &retry_count);
        if (result != 0) {
            set_error("RaSetQpAttrRetryCnt(AI QP) failed: " + std::to_string(result));
            reset();
            return false;
        }
    }
    result = api_->ra_get_qp_attr(qp_handle_, &local_attributes_);
    if (result != 0 || !is_valid_qp_number(local_attributes_.qpn) || !is_valid_psn(local_attributes_.psn)) {
        set_error("RaGetQpAttr failed or returned invalid local QP attributes: " + std::to_string(result));
        reset();
        return false;
    }

    error_.clear();
    return true;
}

bool NpuRaQp::make_qp_info(nds_qp_info *info) {
    if (info == nullptr || !created()) {
        set_error("NPU RA QP has not been created");
        return false;
    }

    *info = {};
    info->qp_num = local_attributes_.qpn;
    info->psn = local_attributes_.psn;
    info->port_num = config_.port_num;
    info->gid_index = static_cast<std::uint16_t>(local_attributes_.gid_index);
    info->path_mtu = config_.path_mtu;
    info->traffic_class = config_.traffic_class;
    info->service_level = config_.service_level;
    info->retry_count = config_.retry_count;
    info->retry_timeout = config_.retry_timeout;
    std::memcpy(info->gid, local_attributes_.gid, sizeof(info->gid));
    error_.clear();
    return true;
}

bool NpuRaQp::register_memory(void *address, std::uint64_t size, int access, nds_ra_mr_info *info, void **mr_handle) {
    int result;

    if (!created() || address == nullptr || size == 0U || info == nullptr || mr_handle == nullptr) {
        set_error("RA memory registration requires a created QP, memory address, nonzero size, and output handle");
        return false;
    }
    *info = {};
    info->address = address;
    info->size = size;
    info->access = access;
    *mr_handle = nullptr;
    result = api_->ra_register_mr(rdev_handle_, info, mr_handle);
    if (result != 0 || *mr_handle == nullptr || info->local_key == 0U || info->remote_key == 0U) {
        set_error("RaRegisterMr failed or returned invalid keys: " + std::to_string(result));
        return false;
    }
    error_.clear();
    return true;
}

bool NpuRaQp::deregister_memory(void *mr_handle) {
    int result;

    if (!created() || mr_handle == nullptr) {
        set_error("RA memory deregistration requires a created QP and MR handle");
        return false;
    }
    result = api_->ra_deregister_mr(rdev_handle_, mr_handle);
    if (result != 0) {
        set_error("RaDeregisterMr failed: " + std::to_string(result));
        return false;
    }
    error_.clear();
    return true;
}

bool NpuRaQp::query_cqe_errors(nds_ra_cqe_error *errors, std::uint32_t *count) {
    unsigned int requested;
    int result;

    if (!created() || api_ == nullptr || api_->ra_rdev_get_cqe_error_list == nullptr || errors == nullptr ||
        count == nullptr || *count == 0U) {
        set_error("CQE-error query requires a created QP, RaRdevGetCqeErrInfoList, output storage, and capacity");
        return false;
    }
    requested = *count;
    result = api_->ra_rdev_get_cqe_error_list(rdev_handle_, errors, &requested);
    if (result != 0) {
        set_error("RaRdevGetCqeErrInfoList failed: " + std::to_string(result));
        return false;
    }
    if (requested > *count) {
        set_error("RaRdevGetCqeErrInfoList returned more entries than the supplied capacity");
        return false;
    }
    *count = requested;
    error_.clear();
    return true;
}

bool NpuRaQp::query_port_status(int *status) {
    int result;

    if (status == nullptr || !created() || api_ == nullptr || api_->ra_rdev_get_port_status == nullptr) {
        set_error("port-status query requires a created QP and RaRdevGetPortStatus");
        return false;
    }
    *status = -1;
    result = api_->ra_rdev_get_port_status(rdev_handle_, status);
    if (result != 0) {
        set_error("RaRdevGetPortStatus failed: " + std::to_string(result));
        return false;
    }
    if (*status < NDS_RA_PORT_STATUS_DOWN || *status > NDS_RA_PORT_STATUS_ACTIVE) {
        set_error("RaRdevGetPortStatus returned an unknown status: " + std::to_string(*status));
        return false;
    }
    error_.clear();
    return true;
}

bool NpuRaQp::query_support_lite(int *support_lite) {
    int result;

    if (support_lite == nullptr || !created() || api_ == nullptr || api_->ra_rdev_get_support_lite == nullptr) {
        set_error("RDMA-lite support query requires a created rdev and RaRdevGetSupportLite");
        return false;
    }
    *support_lite = -1;
    result = api_->ra_rdev_get_support_lite(rdev_handle_, support_lite);
    if (result != 0) {
        set_error("RaRdevGetSupportLite failed: " + std::to_string(result));
        return false;
    }
    if (*support_lite < NDS_RA_LITE_NOT_SUPPORTED || *support_lite > NDS_RA_LITE_ALIGN_2M) {
        set_error("RaRdevGetSupportLite returned an unknown value: " + std::to_string(*support_lite));
        return false;
    }
    error_.clear();
    return true;
}

bool NpuRaQp::query_status(int *status) {
    int result;

    if (status == nullptr || !created() || api_ == nullptr || api_->ra_get_qp_status == nullptr) {
        set_error("QP-status query requires a created QP and RaGetQpStatus");
        return false;
    }
    *status = -1;
    result = api_->ra_get_qp_status(qp_handle_, status);
    if (result != 0) {
        set_error("RaGetQpStatus failed: " + std::to_string(result));
        return false;
    }
    if (*status < NDS_RA_QP_STATUS_NOT_CONNECTED || *status > NDS_RA_QP_STATUS_CONNECTING) {
        set_error("RaGetQpStatus returned an unknown status: " + std::to_string(*status));
        return false;
    }
    error_.clear();
    return true;
}

bool NpuRaQp::connect(const nds_qp_info &peer) {
    nds_ra_typical_qp local_qp{};
    nds_ra_qp_attr peer_attributes{};
    nds_ra_typical_qp remote_qp{};
    int result;

    if (!created()) {
        set_error("NPU RA QP has not been created");
        return false;
    }
    if (connected_) {
        set_error("NPU RA QP is already connected");
        return false;
    }
    if (peer.qp_num == 0U || peer.qp_num > kQpnMask || peer.psn > kPsnMask || peer.port_num == 0U ||
        peer.path_mtu == 0U) {
        set_error("peer endpoint has invalid QP metadata");
        return false;
    }

    if (!build_typical_qp(local_attributes_, config_.traffic_class, config_.service_level, config_.retry_count,
                          config_.retry_timeout, &local_qp)) {
        set_error("local RA QP attributes cannot be converted to a typical QP description");
        return false;
    }
    peer_attributes.qpn = peer.qp_num;
    peer_attributes.psn = peer.psn;
    peer_attributes.gid_index = peer.gid_index;
    std::memcpy(peer_attributes.gid, peer.gid, sizeof(peer_attributes.gid));
    if (!build_typical_qp(peer_attributes, peer.traffic_class, peer.service_level, peer.retry_count, peer.retry_timeout,
                          &remote_qp)) {
        set_error("peer endpoint cannot be converted to a typical QP description");
        return false;
    }

    result = api_->ra_typical_qp_modify(qp_handle_, &local_qp, &remote_qp);
    if (result != 0) {
        set_error("RaTypicalQpModify failed: " + std::to_string(result));
        return false;
    }
    connected_ = true;
    error_.clear();
    return true;
}

void NpuRaQp::reset() noexcept {
    if (api_ != nullptr && qp_handle_ != nullptr) {
        (void)api_->ra_qp_destroy(qp_handle_);
    }
    qp_handle_ = nullptr;
    if (api_ != nullptr && rdev_handle_ != nullptr) {
        (void)api_->ra_rdev_deinit(rdev_handle_, NDS_RA_NOTIFY);
    }
    rdev_handle_ = nullptr;
    api_ = nullptr;
    config_ = {};
    execution_ = NpuExecutionMode::Ra;
    local_attributes_ = {};
    ai_qp_info_ = {};
    send_wr_ids_ = 0U;
    receive_wr_ids_ = 0U;
    posted_send_sge_ = {};
    connected_ = false;
}

nds_ra_api *NpuRaQp::ra_api() const noexcept {
    return api_;
}

void *NpuRaQp::qp_handle() const noexcept {
    return qp_handle_;
}

nds_ra_sge *NpuRaQp::posted_send_sge() noexcept {
    return &posted_send_sge_;
}

bool NpuRaQp::created() const noexcept {
    return qp_handle_ != nullptr;
}

bool NpuRaQp::connected() const noexcept {
    return connected_;
}

const nds_ra_qp_attr &NpuRaQp::local_attributes() const noexcept {
    return local_attributes_;
}

bool NpuRaQp::has_ai_qp_info() const noexcept {
    return (execution_ == NpuExecutionMode::Aicpu || execution_ == NpuExecutionMode::Aiv) &&
           ai_qp_info_.ai_qp_address != 0U;
}

const nds_ra_ai_qp_info &NpuRaQp::ai_qp_info() const noexcept {
    return ai_qp_info_;
}

Result<void> NpuRaQp::set_device_wr_id_storage(std::uint64_t send_address,
                                               std::uint64_t receive_address) {
    if (!has_ai_qp_info() || send_address == 0U || receive_address == 0U)
        return unexpected(ErrorCode::kInvalidArgument,
                          "device WR-ID storage requires an AI QP and two device addresses");
    send_wr_ids_ = send_address;
    receive_wr_ids_ = receive_address;
    return {};
}

Result<nds_device_transport> NpuRaQp::make_device_transport() const {
    if (!has_ai_qp_info() || send_wr_ids_ == 0U || receive_wr_ids_ == 0U)
        return unexpected(ErrorCode::kInvalidArgument,
                          "device transport requires an AI QP and WR-ID storage");
    const auto *source = reinterpret_cast<const nds_ra_ai_data_plane_info *>(ai_qp_info_.data_plane_info);
    if (source->send_wq.buffer_address == 0U || source->receive_wq.buffer_address == 0U)
        return unexpected(ErrorCode::kRa, "HCCP did not return SQ/RQ dataplane information");
    if ((config_.control_flags & NpuRaQpCallerPollsCq) != 0U &&
        (source->send_cq.buffer_address == 0U || source->receive_cq.buffer_address == 0U ||
         ai_qp_info_.ai_scq_address == 0U || ai_qp_info_.ai_rcq_address == 0U)) {
        return unexpected(ErrorCode::kRa, "HCCP did not return caller-owned CQ dataplane information");
    }

    const auto copy_wq = [](const nds_ra_ai_data_plane_wq &input, std::uint64_t wr_ids,
                            bool send) {
        nds_device_work_queue output{};
        output.number = input.wqn;
        output.depth = input.depth;
        output.entry_size = input.wqebb_size;
        output.buffer_address = input.buffer_address;
        output.head_address = input.head_address;
        output.tail_address = input.tail_address;
        output.wr_id_address = wr_ids;
        output.doorbell_mode = send ? NDS_DEVICE_DOORBELL_MMIO : NDS_DEVICE_DOORBELL_RECORD;
        output.doorbell_address = send ? input.doorbell_register_address : input.software_doorbell_address;
        return output;
    };
    const auto copy_cq = [](const nds_ra_ai_data_plane_cq &input) {
        nds_device_completion_queue output{};
        output.number = input.cqn;
        output.depth = input.depth;
        output.entry_size = input.cqe_size;
        output.buffer_address = input.buffer_address;
        output.consumer_address = input.tail_address;
        output.doorbell_mode = NDS_DEVICE_DOORBELL_RECORD;
        output.doorbell_address = input.software_doorbell_address;
        return output;
    };

    nds_device_transport output{};
    output.abi_version = NDS_DEVICE_TRANSPORT_ABI_VERSION;
    output.size = sizeof(output);
    output.control_qp.abi_version = NDS_DEVICE_QP_ABI_VERSION;
    output.control_qp.size = sizeof(output.control_qp);
    output.control_qp.flags = (config_.control_flags & NpuRaQpCallerPollsCq) != 0U ?
                          static_cast<std::uint32_t>(NDS_DEVICE_QP_CALLER_POLLS_CQ) : 0U;
    output.control_qp.qp_mode = config_.ai_qp_mode;
    output.control_qp.service_level = config_.service_level;
    output.control_qp.provider_qp_address = ai_qp_info_.ai_qp_address;
    output.control_qp.provider_send_cq_address = ai_qp_info_.ai_scq_address;
    output.control_qp.provider_receive_cq_address = ai_qp_info_.ai_rcq_address;
    output.control_qp.send_queue = copy_wq(source->send_wq, send_wr_ids_, true);
    output.control_qp.receive_queue = copy_wq(source->receive_wq, receive_wr_ids_, false);
    output.control_qp.send_cq = copy_cq(source->send_cq);
    output.control_qp.receive_cq = copy_cq(source->receive_cq);
    return output;
}

const NpuRaQpConfig &NpuRaQp::config() const noexcept {
    return config_;
}

NpuExecutionMode NpuRaQp::execution_mode() const noexcept {
    return execution_;
}

const std::string &NpuRaQp::error() const noexcept {
    return error_;
}

}  // namespace nds
