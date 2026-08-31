#ifndef NDS_CLIENT_BACKEND_DISPATCHER_HH
#define NDS_CLIENT_BACKEND_DISPATCHER_HH

#include "aicpu/launcher.hh"
#include "aiv/launcher.hh"
#include "backend_mode.hh"
#include "launch_config.hh"
#include "ra/launcher.hh"

#include "endpoint.hh"
#include "result.hh"
#include "device_verbs.h"

#include <cstdint>
#include <memory>
#include <string>

namespace nds::client {

class Runtime;

/* Selects one loaded backend. Callers own ACL stream/event lifecycle. */
class BackendLauncher {
public:
    ~BackendLauncher();
    BackendLauncher() = default;
    BackendLauncher(const BackendLauncher &) = delete;
    BackendLauncher &operator=(const BackendLauncher &) = delete;
    BackendLauncher(BackendLauncher &&) noexcept = default;
    BackendLauncher &operator=(BackendLauncher &&) noexcept = default;
    Result<void> open(Runtime *runtime, BackendMode mode, const std::string &artifact);

    Result<void> post_send(const LaunchConfig &launch_config, const NdsDeviceQp &qp, const NdsDeviceSendWr &wr,
                           std::int32_t timeout_ms);
    Result<void> post_recv(const LaunchConfig &launch_config, const NdsDeviceQp &qp, const NdsDeviceRecvWr &wr,
                           std::int32_t timeout_ms);
    Result<std::uint32_t> poll_cq(const LaunchConfig &launch_config, const NdsDeviceQp &qp, bool send_cq,
                                  std::uint32_t max_completions, NdsDeviceWc *completions, std::int32_t timeout_ms);

private:
    BackendMode mode_{BackendMode::Ra};
    Runtime *runtime_{};
    std::unique_ptr<::nds::RaLauncher> ra_;
    std::unique_ptr<::nds::AivLauncher> aiv_;
    std::unique_ptr<::nds::AicpuLauncher> aicpu_;
};

}  // namespace nds::client

#endif
