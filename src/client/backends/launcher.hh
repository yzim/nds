#ifndef NDS_CLIENT_BACKENDS_LAUNCHER_HH
#define NDS_CLIENT_BACKENDS_LAUNCHER_HH

#include "backend_mode.hh"
#include "launch_config.hh"

#include "result.hh"
#include "device_verbs.h"

#include <cstdint>
#include <memory>
#include <string>

namespace nds::client {

class Runtime;
class Launcher;

/* Non-owning temporary launch view, analogous to <<< >>>. */
class ConfiguredLauncherView {
public:
    Result<void> post_send(const NdsDeviceQp &qp, const NdsDeviceSendWr &wr) const;
    Result<void> post_recv(const NdsDeviceQp &qp, const NdsDeviceRecvWr &wr) const;
    Result<std::uint32_t> poll_cq(const NdsDeviceQp &qp, bool send_cq, std::uint32_t max_completions,
                                  NdsDeviceWc *completions) const;

private:
    friend class Launcher;
    ConfiguredLauncherView(const Launcher *launcher, const LaunchConfig &config);

    const Launcher *launcher_{};
    LaunchConfig config_{};
};

/* Common host interface for exactly one loaded RA, AIV, or AICPU backend. */
class Launcher {
public:
    virtual ~Launcher() = default;
    Launcher(const Launcher &) = delete;
    Launcher &operator=(const Launcher &) = delete;

    static Result<std::unique_ptr<Launcher>> open(Runtime *runtime, BackendMode mode, const std::string &artifact);

    Result<void> post_send(const NdsDeviceQp &qp, const NdsDeviceSendWr &wr) const;
    Result<void> post_recv(const NdsDeviceQp &qp, const NdsDeviceRecvWr &wr) const;
    Result<std::uint32_t> poll_cq(const NdsDeviceQp &qp, bool send_cq, std::uint32_t max_completions,
                                  NdsDeviceWc *completions) const;
    ConfiguredLauncherView with_config(const LaunchConfig &config) const;

protected:
    Launcher() = default;

    virtual Result<void> post_send_with_config(const LaunchConfig &config, const NdsDeviceQp &qp,
                                               const NdsDeviceSendWr &wr) const = 0;
    virtual Result<void> post_recv_with_config(const LaunchConfig &config, const NdsDeviceQp &qp,
                                               const NdsDeviceRecvWr &wr) const = 0;
    virtual Result<std::uint32_t> poll_cq_with_config(const LaunchConfig &config, const NdsDeviceQp &qp, bool send_cq,
                                                      std::uint32_t max_completions,
                                                      NdsDeviceWc *completions) const = 0;

private:
    friend class ConfiguredLauncherView;
};

}  // namespace nds::client

#endif
