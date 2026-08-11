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
    if (api.ra_rdev_init == nullptr || api.ra_rdev_deinit == nullptr || api.ra_typical_qp_create == nullptr ||
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
    result = api_->ra_rdev_init(NDS_RA_NETWORK_OFFLINE, NDS_RA_NOTIFY, rdev, &rdev_handle_);
    if (result != 0 || rdev_handle_ == nullptr) {
        set_error("RaRdevInit failed: " + std::to_string(result));
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
