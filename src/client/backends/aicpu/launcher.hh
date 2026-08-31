#ifndef NDS_CLIENT_BACKEND_AICPU_LAUNCHER_HH
#define NDS_CLIENT_BACKEND_AICPU_LAUNCHER_HH

#include "result.hh"
#include "backends/launch_config.hh"

#include <acl/acl_rt.h>

#include <cstddef>
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
    /*
     * AscendCL copies these host argument bytes during submission.  The
     * request therefore needs no intermediate allocation in global memory.
     */
    /* Returns the raw ACL status. The caller owns stream ordering and waits. */
    int launch(const char *kernel_name, const client::LaunchConfig &config, void *arguments, std::size_t argument_size);
    void reset() noexcept;
    bool loaded() const noexcept;

private:
    aclrtBinHandle binary_{};
    std::unordered_map<std::string, aclrtFuncHandle> functions_;
};

}  // namespace nds

#endif
