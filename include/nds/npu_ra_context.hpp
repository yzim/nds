#ifndef NDS_NPU_RA_CONTEXT_HPP
#define NDS_NPU_RA_CONTEXT_HPP

#include "nds/acl_loader.h"
#include "nds/ra_loader.h"
#include "nds/runtime_loader.h"

#include <cstddef>
#include <cstdint>
#include <string>

namespace nds {

/*
 * Owns the single-NPU direct RA process lifecycle.  It intentionally does not
 * initialize HCOMM/HCCL or consume a rank table: the peer is a CPU verbs
 * process, not another NPU rank.
 */
struct NpuRaContextConfig {
    std::string ascendcl_library;
    std::string runtime_library;
    std::string ra_library;
    std::uint32_t logical_device_id{NDS_RA_PHY_ID_NPU0};
    std::uint32_t physical_device_id{NDS_RA_PHY_ID_NPU0};
    std::int32_t hdc_type{NDS_RA_HDC_SERVICE_TYPE_RDMA_V2};
};

class NpuRaContext {
public:
    NpuRaContext() = default;
    ~NpuRaContext();
    NpuRaContext(const NpuRaContext &) = delete;
    NpuRaContext &operator=(const NpuRaContext &) = delete;
    NpuRaContext(NpuRaContext &&) = delete;
    NpuRaContext &operator=(NpuRaContext &&) = delete;

    bool initialize(const NpuRaContextConfig &config);
    void reset() noexcept;

    bool initialized() const noexcept;
    bool allocate_device_memory(std::size_t size, void **device_ptr);
    bool free_device_memory(void *device_ptr);
    bool copy_host_to_device(void *device_ptr, const void *host_ptr, std::size_t size);
    bool zero_device_memory(void *device_ptr, std::size_t size);
    bool submit_rdma_doorbell(std::uint32_t db_index, std::uint64_t db_info);
    nds_ra_api &ra_api() noexcept;
    nds_acl_api &acl_api() noexcept;
    const std::string &error() const noexcept;

private:
    void set_error(std::string message);

    NpuRaContextConfig config_{};
    nds_acl_api acl_{};
    nds_runtime_api runtime_{};
    nds_ra_api ra_{};
    bool acl_initialized_{false};
    bool net_service_open_{false};
    bool ra_initialized_{false};
    bool initialized_{false};
    std::string error_;
};

} // namespace nds

#endif
