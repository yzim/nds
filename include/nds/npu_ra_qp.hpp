#ifndef NDS_NPU_RA_QP_HPP
#define NDS_NPU_RA_QP_HPP

#include "nds/ra_loader.h"
#include "nds/rdma_wire_codec.h"

#include <cstdint>
#include <string>

namespace nds {

/*
 * NPU RoCE policy supplied by the deployment.  It deliberately contains only
 * transport values; CANN process/HCOMM bootstrap remains CannContext's job.
 */
struct NpuRaQpConfig {
    std::uint32_t physical_device_id{NDS_RA_PHY_ID_NPU0};
    std::string local_ipv4;
    std::uint16_t port_num{1};
    std::uint16_t path_mtu{1024};
    std::uint32_t traffic_class{0};
    std::uint32_t service_level{0};
    std::uint32_t retry_count{7};
    std::uint32_t retry_timeout{14};
};

/*
 * Owns only RA resource-level state after HCOMM has initialized global CANN
 * communication state.  It never calls RaInit/RaDeinit and does not own the
 * dynamically loaded libra.so handle.
 */
class NpuRaQp {
public:
    NpuRaQp() = default;
    ~NpuRaQp();
    NpuRaQp(const NpuRaQp &) = delete;
    NpuRaQp &operator=(const NpuRaQp &) = delete;
    NpuRaQp(NpuRaQp &&) = delete;
    NpuRaQp &operator=(NpuRaQp &&) = delete;

    bool create(nds_ra_api &api, const NpuRaQpConfig &config);
    bool make_qp_only_endpoint(nds_rc_endpoint &endpoint);
    bool make_data_ready_endpoint(std::uint64_t address, std::uint32_t rkey, nds_rc_endpoint &endpoint);
    bool connect(const nds_rc_endpoint &peer);
    void reset() noexcept;

    bool created() const noexcept;
    bool connected() const noexcept;
    const nds_ra_qp_attr &local_attributes() const noexcept;
    const NpuRaQpConfig &config() const noexcept;
    const std::string &error() const noexcept;

private:
    bool build_typical_qp(const nds_ra_qp_attr &attributes,
                          std::uint32_t traffic_class,
                          std::uint32_t service_level,
                          std::uint32_t retry_count,
                          std::uint32_t retry_timeout,
                          nds_ra_typical_qp &out) const;
    void set_error(std::string message);

    nds_ra_api *api_{nullptr};
    NpuRaQpConfig config_{};
    void *rdev_handle_{nullptr};
    void *qp_handle_{nullptr};
    nds_ra_qp_attr local_attributes_{};
    bool connected_{false};
    std::string error_;
};

} // namespace nds

#endif
