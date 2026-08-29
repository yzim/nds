#ifndef NDS_AIV_LAUNCHER_HH
#define NDS_AIV_LAUNCHER_HH

#include "result.hh"

#include <acl/acl_rt.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>

namespace nds {

/* Loads one NDS AIV artifact and launches ABI-defined kernel entries. */
class AivLauncher {
public:
    AivLauncher() = default;
    ~AivLauncher();
    AivLauncher(const AivLauncher &) = delete;
    AivLauncher &operator=(const AivLauncher &) = delete;

    Result<void> load(const std::string &kernel_path);
    Result<void> launch_and_wait(const char *kernel_name, void *arguments, std::size_t argument_size,
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
