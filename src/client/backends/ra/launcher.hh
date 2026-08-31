#ifndef NDS_CLIENT_BACKEND_RA_LAUNCHER_HH
#define NDS_CLIENT_BACKEND_RA_LAUNCHER_HH

#include "backends/launcher.hh"

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>

#include "device_verbs.h"

namespace nds::client {

/* Host-side wrapper for the dynamically loaded RA verbs entry points. */
class RaLauncher final : public Launcher {
public:
    RaLauncher();
    ~RaLauncher();
    RaLauncher(const RaLauncher &) = delete;
    RaLauncher &operator=(const RaLauncher &) = delete;
    RaLauncher(RaLauncher &&) noexcept = default;
    RaLauncher &operator=(RaLauncher &&) noexcept = default;

    static Result<std::unique_ptr<Launcher>> open(const std::string &backend_path);

private:
    Result<void> post_send_with_config(const LaunchConfig &config, const NdsDeviceQp &qp,
                                       const NdsDeviceSendWr &wr) const override;
    Result<void> post_recv_with_config(const LaunchConfig &config, const NdsDeviceQp &qp,
                                       const NdsDeviceRecvWr &wr) const override;
    Result<std::uint32_t> poll_cq_with_config(const LaunchConfig &config, const NdsDeviceQp &qp, bool send_cq,
                                              std::uint32_t max_completions, NdsDeviceWc *wc) const override;

    Result<void> load(const std::string &backend_path);
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace nds::client

#endif
