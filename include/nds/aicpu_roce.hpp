#ifndef NDS_AICPU_ROCE_HPP
#define NDS_AICPU_ROCE_HPP

#include "nds/acl_loader.h"
#include "nds/aicpu_roce_abi.h"

#include <cstdint>
#include <string>

namespace nds {

/*
 * Loader for NDS's own AICPU package.  The package contains only
 * NdsAicpuRdmaPost: one signaled RDMA WRITE, READ, or SEND post followed by
 * its RNIC doorbell. It deliberately has no HCOMM flag protocol, rank state,
 * or reciprocal peer dependency.
 */
struct AicpuRdmaPostRequest {
    std::uint32_t opcode{NDS_AICPU_RDMA_WRITE};
    std::uint32_t db_index{};
    std::uint64_t ai_qp_address{};
    std::uint32_t local_key{};
    std::uint32_t remote_key{};
    std::uint64_t local_address{};
    std::uint64_t remote_address{};
    std::uint64_t data_size{};
    std::uint64_t wr_id{};
    std::uint16_t launch_timeout_seconds{5U};
};

class AicpuRdmaPostLauncher {
public:
    AicpuRdmaPostLauncher() = default;
    ~AicpuRdmaPostLauncher();
    AicpuRdmaPostLauncher(const AicpuRdmaPostLauncher &) = delete;
    AicpuRdmaPostLauncher &operator=(const AicpuRdmaPostLauncher &) = delete;

    bool load(nds_acl_api &acl, const std::string &kernel_config_path);
    bool launch_and_wait(const AicpuRdmaPostRequest &request, std::int32_t completion_timeout_ms);
    void reset() noexcept;
    bool loaded() const noexcept;
    const std::string &error() const noexcept;

private:
    void set_error(std::string message);

    nds_acl_api *acl_{};
    nds_acl_bin_handle binary_{};
    nds_acl_stream stream_{};
    std::string error_;
};

} // namespace nds

#endif
