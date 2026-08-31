#ifndef NDS_CLIENT_BACKEND_AIV_LAUNCHER_HH
#define NDS_CLIENT_BACKEND_AIV_LAUNCHER_HH

#include "result.hh"
#include "backends/launch_config.hh"

#include <acl/acl_rt.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>

namespace nds {

/* Loads and launches NDS AIV device entries through AscendCL. */
class AivLauncher {
public:
    AivLauncher() = default;
    ~AivLauncher();
    AivLauncher(const AivLauncher &) = delete;
    AivLauncher &operator=(const AivLauncher &) = delete;

    Result<void> load(const std::string &kernel_path);
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
