#include "nds/npu_ra_qp.hpp"

#include <arpa/inet.h>

#include <cstring>
#include <limits>
#include <utility>

namespace nds {
namespace {

constexpr std::uint32_t kQpnMask = 0x00ffffffU;
constexpr std::uint32_t kPsnMask = 0x00ffffffU;

bool is_valid_qp_number(std::uint32_t value)
{
    return value != 0U && value <= kQpnMask;
}

bool is_valid_psn(std::uint32_t value)
{
    return value <= kPsnMask;
}

} // namespace

NpuRaQp::~NpuRaQp()
{
    reset();
}

void NpuRaQp::set_error(std::string message)
{
    error_ = std::move(message);
}

bool NpuRaQp::build_typical_qp(const nds_ra_qp_attr &attributes,
                               std::uint32_t traffic_class,
                               std::uint32_t service_level,
                               std::uint32_t retry_count,
                               std::uint32_t retry_timeout,
                               nds_ra_typical_qp &out) const
{
    if (!is_valid_qp_number(attributes.qpn) || !is_valid_psn(attributes.psn)) {
        return false;
    }

    out = {};
    out.qpn = attributes.qpn;
    out.psn = attributes.psn;
    out.gid_index = attributes.gid_index;
    std::memcpy(out.gid, attributes.gid, sizeof(out.gid));
    out.traffic_class = traffic_class;
    out.service_level = service_level;
    out.retry_count = retry_count;
    out.retry_timeout = retry_timeout;
    return true;
}

bool NpuRaQp::create(nds_ra_api &api, const NpuRaQpConfig &config)
{
    nds_ra_rdev rdev{};
    nds_ra_rdev_init_info rdev_init{};
    nds_ra_typical_qp initial_qp{};
    int result;

    if (created()) {
        set_error("NPU RA QP is already created");
        return false;
    }
    if (config.local_ipv4.empty() || config.port_num == 0U || config.path_mtu == 0U) {
        set_error("NPU RA QP requires a local IPv4 address, nonzero port, and nonzero path MTU");
        return false;
    }
    if (api.ra_rdev_init_v2 == nullptr || api.ra_rdev_deinit == nullptr || api.ra_typical_qp_create == nullptr ||
        api.ra_qp_destroy == nullptr || api.ra_get_qp_attr == nullptr) {
        set_error("RA API is missing a required rdev/QP/query operation");
        return false;
    }

    rdev.phy_id = config.physical_device_id;
    rdev.family = AF_INET;
    if (inet_pton(AF_INET, config.local_ipv4.c_str(), &rdev.local_ip.ipv4) != 1) {
        set_error("NPU RA QP local IPv4 address is invalid: " + config.local_ipv4);
        return false;
    }

    api_ = &api;
    config_ = config;
    rdev_init.mode = NDS_RA_NETWORK_OFFLINE;
    rdev_init.notify_type = NDS_RA_NOTIFY;
    rdev_init.enabled_910a_lite = false;
    rdev_init.disabled_lite_thread = true;
    rdev_init.enabled_2mb_lite = false;
    result = api_->ra_rdev_init_v2(rdev_init, rdev, &rdev_handle_);
    if (result != 0 || rdev_handle_ == nullptr) {
        set_error("RaRdevInitV2(disabledLiteThread=true) failed: " + std::to_string(result));
        reset();
        return false;
    }
    result = api_->ra_typical_qp_create(rdev_handle_, NDS_RA_QP_FLAG_RC, NDS_RA_QP_MODE_OPBASE, &initial_qp,
                                         &qp_handle_);
    if (result != 0 || qp_handle_ == nullptr) {
        set_error("RaTypicalQpCreate failed: " + std::to_string(result));
        reset();
        return false;
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

bool NpuRaQp::make_qp_only_endpoint(nds_rc_endpoint &endpoint)
{
    if (!created()) {
        set_error("NPU RA QP has not been created");
        return false;
    }

    endpoint = {};
    endpoint.flags = NDS_ENDPOINT_FLAG_QP_ONLY;
    endpoint.qp_num = local_attributes_.qpn;
    endpoint.psn = local_attributes_.psn;
    endpoint.port_num = config_.port_num;
    endpoint.gid_index = static_cast<std::uint16_t>(local_attributes_.gid_index);
    endpoint.path_mtu = config_.path_mtu;
    endpoint.traffic_class = config_.traffic_class;
    endpoint.service_level = config_.service_level;
    endpoint.retry_count = config_.retry_count;
    endpoint.retry_timeout = config_.retry_timeout;
    std::memcpy(endpoint.gid, local_attributes_.gid, sizeof(endpoint.gid));
    error_.clear();
    return true;
}

bool NpuRaQp::make_data_ready_endpoint(std::uint64_t address, std::uint32_t rkey,
                                       nds_rc_endpoint &endpoint)
{
    if (rkey == 0U || address == 0U) {
        set_error("data-ready NPU endpoint requires a nonzero address and registered-memory rkey");
        return false;
    }
    if (!make_qp_only_endpoint(endpoint)) {
        return false;
    }
    endpoint.flags = NDS_ENDPOINT_FLAG_DATA_READY;
    endpoint.rkey = rkey;
    endpoint.address = address;
    /* RA MR access flags will be propagated when the data-plane path is added. */
    endpoint.access_flags = 1U;
    return true;
}

bool NpuRaQp::register_memory(void *address, std::uint64_t size, int access, nds_ra_mr_info &info,
                                void **mr_handle)
{
    int result;

    if (!created() || address == nullptr || size == 0U || mr_handle == nullptr) {
        set_error("RA memory registration requires a created QP, memory address, nonzero size, and output handle");
        return false;
    }
    info = {};
    info.address = address;
    info.size = size;
    info.access = access;
    *mr_handle = nullptr;
    result = api_->ra_register_mr(rdev_handle_, &info, mr_handle);
    if (result != 0 || *mr_handle == nullptr || info.local_key == 0U || info.remote_key == 0U) {
        set_error("RaRegisterMr failed or returned invalid keys: " + std::to_string(result));
        return false;
    }
    error_.clear();
    return true;
}

bool NpuRaQp::deregister_memory(void *mr_handle)
{
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

bool NpuRaQp::post_rdma_write(const nds_ra_sge &source, std::uint64_t remote_address,
                              std::uint32_t remote_key, bool signaled, nds_ra_send_response &response)
{
    nds_ra_sge local = source;
    nds_ra_send_wr wr{};
    int result;

    if (!connected_ || local.address == 0U || local.length == 0U || local.local_key == 0U ||
        remote_address == 0U || remote_key == 0U) {
        set_error("RDMA write requires a connected QP, one valid local SGE, and valid remote memory metadata");
        return false;
    }
    wr.buffers = &local;
    wr.buffer_count = 1U;
    wr.remote_address = remote_address;
    wr.remote_key = remote_key;
    wr.opcode = NDS_RA_WR_RDMA_WRITE;
    wr.send_flags = signaled ? NDS_RA_SEND_SIGNALED : 0;
    response = {};
    result = api_->ra_typical_send_wr(qp_handle_, &wr, &response);
    if (result != 0) {
        set_error("RaTypicalSendWr failed: " + std::to_string(result));
        return false;
    }
    error_.clear();
    return true;
}

bool NpuRaQp::query_cqe_errors(nds_ra_cqe_error *errors, std::uint32_t &count)
{
    unsigned int requested;
    int result;

    if (!created() || api_ == nullptr || api_->ra_rdev_get_cqe_error_list == nullptr || errors == nullptr ||
        count == 0U) {
        set_error("CQE-error query requires a created QP, RaRdevGetCqeErrInfoList, output storage, and capacity");
        return false;
    }
    requested = count;
    result = api_->ra_rdev_get_cqe_error_list(rdev_handle_, errors, &requested);
    if (result != 0) {
        set_error("RaRdevGetCqeErrInfoList failed: " + std::to_string(result));
        return false;
    }
    if (requested > count) {
        set_error("RaRdevGetCqeErrInfoList returned more entries than the supplied capacity");
        return false;
    }
    count = requested;
    error_.clear();
    return true;
}

bool NpuRaQp::query_port_status(int &status)
{
    int result;

    if (!created() || api_ == nullptr || api_->ra_rdev_get_port_status == nullptr) {
        set_error("port-status query requires a created QP and RaRdevGetPortStatus");
        return false;
    }
    status = -1;
    result = api_->ra_rdev_get_port_status(rdev_handle_, &status);
    if (result != 0) {
        set_error("RaRdevGetPortStatus failed: " + std::to_string(result));
        return false;
    }
    if (status < NDS_RA_PORT_STATUS_DOWN || status > NDS_RA_PORT_STATUS_ACTIVE) {
        set_error("RaRdevGetPortStatus returned an unknown status: " + std::to_string(status));
        return false;
    }
    error_.clear();
    return true;
}

bool NpuRaQp::query_support_lite(int &support_lite)
{
    int result;

    if (!created() || api_ == nullptr || api_->ra_rdev_get_support_lite == nullptr) {
        set_error("RDMA-lite support query requires a created rdev and RaRdevGetSupportLite");
        return false;
    }
    support_lite = -1;
    result = api_->ra_rdev_get_support_lite(rdev_handle_, &support_lite);
    if (result != 0) {
        set_error("RaRdevGetSupportLite failed: " + std::to_string(result));
        return false;
    }
    if (support_lite < NDS_RA_LITE_NOT_SUPPORTED || support_lite > NDS_RA_LITE_ALIGN_2M) {
        set_error("RaRdevGetSupportLite returned an unknown value: " + std::to_string(support_lite));
        return false;
    }
    error_.clear();
    return true;
}

bool NpuRaQp::query_status(int &status)
{
    int result;

    if (!created() || api_ == nullptr || api_->ra_get_qp_status == nullptr) {
        set_error("QP-status query requires a created QP and RaGetQpStatus");
        return false;
    }
    status = -1;
    result = api_->ra_get_qp_status(qp_handle_, &status);
    if (result != 0) {
        set_error("RaGetQpStatus failed: " + std::to_string(result));
        return false;
    }
    if (status < NDS_RA_QP_STATUS_NOT_CONNECTED || status > NDS_RA_QP_STATUS_CONNECTING) {
        set_error("RaGetQpStatus returned an unknown status: " + std::to_string(status));
        return false;
    }
    error_.clear();
    return true;
}

int NpuRaQp::poll_send_completions(nds_ra_completion *completions, std::uint32_t max_entries)
{
    int result;

    if (!created() || completions == nullptr || max_entries == 0U) {
        set_error("send-CQ polling requires a created QP, output completion storage, and nonzero entry count");
        return -1;
    }
    result = api_->ra_poll_cq(qp_handle_, true, max_entries, completions);
    if (result < 0) {
        set_error("RaPollCq(send) failed: " + std::to_string(result));
        return -1;
    }
    error_.clear();
    return result;
}

bool NpuRaQp::connect(const nds_rc_endpoint &peer)
{
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
    if ((peer.flags & ~NDS_ENDPOINT_FLAG_ALL) != 0U || peer.flags == 0U ||
        (peer.flags & NDS_ENDPOINT_FLAG_ALL) == NDS_ENDPOINT_FLAG_ALL || peer.qp_num == 0U ||
        peer.qp_num > kQpnMask || peer.psn > kPsnMask || peer.port_num == 0U || peer.path_mtu == 0U) {
        set_error("peer endpoint has invalid QP metadata");
        return false;
    }

    if (!build_typical_qp(local_attributes_, config_.traffic_class, config_.service_level, config_.retry_count,
                          config_.retry_timeout, local_qp)) {
        set_error("local RA QP attributes cannot be converted to a typical QP description");
        return false;
    }
    peer_attributes.qpn = peer.qp_num;
    peer_attributes.psn = peer.psn;
    peer_attributes.gid_index = peer.gid_index;
    std::memcpy(peer_attributes.gid, peer.gid, sizeof(peer_attributes.gid));
    if (!build_typical_qp(peer_attributes, peer.traffic_class, peer.service_level, peer.retry_count,
                          peer.retry_timeout, remote_qp)) {
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

void NpuRaQp::reset() noexcept
{
    if (api_ != nullptr && qp_handle_ != nullptr) {
        (void)api_->ra_qp_destroy(qp_handle_);
    }
    qp_handle_ = nullptr;
    if (api_ != nullptr && rdev_handle_ != nullptr) {
        (void)api_->ra_rdev_deinit(rdev_handle_, NDS_RA_NOTIFY);
    }
    rdev_handle_ = nullptr;
    api_ = nullptr;
    local_attributes_ = {};
    connected_ = false;
}

bool NpuRaQp::created() const noexcept
{
    return qp_handle_ != nullptr;
}

bool NpuRaQp::connected() const noexcept
{
    return connected_;
}

const nds_ra_qp_attr &NpuRaQp::local_attributes() const noexcept
{
    return local_attributes_;
}

const NpuRaQpConfig &NpuRaQp::config() const noexcept
{
    return config_;
}

const std::string &NpuRaQp::error() const noexcept
{
    return error_;
}

} // namespace nds
