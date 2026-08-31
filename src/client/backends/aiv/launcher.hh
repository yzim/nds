#ifndef NDS_CLIENT_BACKEND_AIV_LAUNCHER_HH
#define NDS_CLIENT_BACKEND_AIV_LAUNCHER_HH

#include "backends/launch_config.hh"
#include "backends/launcher.hh"

#include <acl/acl_rt.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>

namespace nds::client {

class Runtime;

/* Loads and launches NDS AIV device entries through AscendCL. */
class AivLauncher final : public Launcher {
public:
    AivLauncher() = default;
    ~AivLauncher();
    AivLauncher(const AivLauncher &) = delete;
    AivLauncher &operator=(const AivLauncher &) = delete;

    static Result<std::unique_ptr<Launcher>> open(Runtime *runtime, const std::string &kernel_path);
    /* Returns the raw ACL status. The caller owns stream ordering and waits. */
    int launch(const char *kernel_name, const LaunchConfig &config, void *arguments, std::size_t argument_size) const;
    void reset() noexcept;
    bool loaded() const noexcept;

private:
    Result<void> post_send_with_config(const LaunchConfig &config, const NdsDeviceQp &qp,
                                       const NdsDeviceSendWr &wr) const override;
    Result<void> post_recv_with_config(const LaunchConfig &config, const NdsDeviceQp &qp,
                                       const NdsDeviceRecvWr &wr) const override;
    Result<std::uint32_t> poll_cq_with_config(const LaunchConfig &config, const NdsDeviceQp &qp, bool send_cq,
                                              std::uint32_t max_completions, NdsDeviceWc *completions) const override;
    Result<void> load(const std::string &kernel_path);

    Runtime *runtime_{};
    aclrtBinHandle binary_{};
    mutable std::unordered_map<std::string, aclrtFuncHandle> functions_;
};

}  // namespace nds::client

#endif
