#ifndef NDS_CLIENT_BACKEND_AICPU_LAUNCHER_HH
#define NDS_CLIENT_BACKEND_AICPU_LAUNCHER_HH

#include "result.hh"

#include <acl/acl_rt.h>

#include <cstdint>
#include <string>
#include <unordered_map>

namespace nds {

/* Loads and launches the installed NDS AICPU package through AscendCL. */
class AicpuLauncher {
public:
    AicpuLauncher() = default;
    ~AicpuLauncher();
    AicpuLauncher(const AicpuLauncher &) = delete;
    AicpuLauncher &operator=(const AicpuLauncher &) = delete;

    Result<void> load(const std::string &kernel_path);
    Result<void> launch(const char *kernel_name, std::uint64_t args_gm_addr);
    Result<void> synchronize(std::int32_t completion_timeout_ms);
    Result<void> launch_and_wait(const char *kernel_name, std::uint64_t args_gm_addr,
                                 std::int32_t completion_timeout_ms);
    void reset() noexcept;
    bool loaded() const noexcept;

private:
    aclrtBinHandle binary_{};
    aclrtStream stream_{};
    std::unordered_map<std::string, aclrtFuncHandle> functions_;
};

}  // namespace nds

#endif
