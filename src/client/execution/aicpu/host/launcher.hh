#ifndef NDS_AICPU_LAUNCHER_HH
#define NDS_AICPU_LAUNCHER_HH

#include "nds/acl_loader.h"
#include "nds/result.hh"

#include <cstdint>
#include <string>
#include <unordered_map>

namespace nds {

/* Loads one standard-CP1 AICPU package and launches its void *args entries. */
class AicpuLauncher {
public:
    AicpuLauncher() = default;
    ~AicpuLauncher();
    AicpuLauncher(const AicpuLauncher &) = delete;
    AicpuLauncher &operator=(const AicpuLauncher &) = delete;

    Result<void> load(NdsAclApi *acl, const std::string &kernel_config_path);
    Result<void> launch_and_wait(const char *kernel_name, std::uint64_t args_gm_addr,
                                 std::int32_t completion_timeout_ms);
    void reset() noexcept;
    bool loaded() const noexcept;

private:
    NdsAclApi *acl_{};
    NdsAclBinHandle binary_{};
    NdsAclStream stream_{};
    std::unordered_map<std::string, NdsAclFuncHandle> functions_;
};

}  // namespace nds

#endif
