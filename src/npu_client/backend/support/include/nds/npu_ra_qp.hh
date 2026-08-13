#ifndef NDS_NPU_RA_QP_HPP
#define NDS_NPU_RA_QP_HPP

#include "nds/ra_loader.h"
#include "nds/rdma_wire_codec.h"

#include <cstdint>
#include <string>

namespace nds {

enum class NpuBackendMode {
    HostRa,
    Aicpu,
    Aiv,
};

/*
 * NPU RoCE policy supplied by the deployment. It deliberately contains only
 * public RA transport values; CANN process/HCOMM bootstrap remains
 * NpuRaContext's job. `path_mtu` is sent in NDS's diagnostic endpoint record,
 * but HCCP v9.0.0 TypicalQp has no MTU field, so it does not configure the
 * NPU runtime's Lite-QP packet MTU.
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
    NpuBackendMode backend{NpuBackendMode::HostRa};
};

/*
 * Owns the RA rdev and QP below the direct NPU process context. It never
 * calls RaInit/RaDeinit and does not own the dynamically loaded libra.so handle.
 */
class NpuRaQp {
public:
    NpuRaQp() = default;
    ~NpuRaQp();
    NpuRaQp(const NpuRaQp &) = delete;
    NpuRaQp &operator=(const NpuRaQp &) = delete;
    NpuRaQp(NpuRaQp &&) = delete;
    NpuRaQp &operator=(NpuRaQp &&) = delete;

    bool create(nds_ra_api *api, const NpuRaQpConfig &config);
    bool make_endpoint(nds_rc_endpoint *endpoint);
    bool register_memory(void *address, std::uint64_t size, int access, nds_ra_mr_info *info, void **mr_handle);
    bool deregister_memory(void *mr_handle);
    bool post_send(const nds_ra_sge &source, std::uint32_t opcode, std::uint64_t remote_address,
                   std::uint32_t remote_key, bool signaled, nds_ra_send_response *response);
    bool post_rdma_write(const nds_ra_sge &source, std::uint64_t remote_address, std::uint32_t remote_key,
                         bool signaled, nds_ra_send_response *response);
    int poll_send_completions(nds_ra_completion *completions, std::uint32_t max_entries);
    bool query_port_status(int *status);
    bool query_support_lite(int *support_lite);
    bool query_status(int *status);
    bool query_cqe_errors(nds_ra_cqe_error *errors, std::uint32_t *count);
    bool connect(const nds_rc_endpoint &peer);
    void reset() noexcept;

    bool created() const noexcept;
    bool connected() const noexcept;
    const nds_ra_qp_attr &local_attributes() const noexcept;
    bool has_ai_qp_info() const noexcept;
    const nds_ra_ai_qp_info &ai_qp_info() const noexcept;
    const NpuRaQpConfig &config() const noexcept;
    const std::string &error() const noexcept;

private:
    bool build_typical_qp(const nds_ra_qp_attr &attributes, std::uint32_t traffic_class, std::uint32_t service_level,
                          std::uint32_t retry_count, std::uint32_t retry_timeout, nds_ra_typical_qp *out) const;
    void set_error(std::string message);

    nds_ra_api *api_{nullptr};
    NpuRaQpConfig config_{};
    void *rdev_handle_{nullptr};
    void *qp_handle_{nullptr};
    nds_ra_qp_attr local_attributes_{};
    nds_ra_ai_qp_info ai_qp_info_{};
    bool connected_{false};
    std::string error_;
};

}  // namespace nds

#endif
