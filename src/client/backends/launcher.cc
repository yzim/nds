#include "launcher.hh"

#include "aicpu/launcher.hh"
#include "aiv/launcher.hh"
#include "ra/launcher.hh"
#include "runtime.hh"

namespace nds::client {

ConfiguredLauncherView::ConfiguredLauncherView(const Launcher *launcher, const LaunchConfig &config)
    : launcher_(launcher), config_(config) {}

Result<void> ConfiguredLauncherView::post_send(const NdsDeviceQp &qp, const NdsDeviceSendWr &wr) const {
    if (launcher_ == nullptr)
        return Error{ErrorCode::kInvalidArgument, "configured launcher is empty"};
    if (!config_.sync)
        return Error{ErrorCode::kInvalidArgument, "asynchronous launcher calls are not supported"};
    return launcher_->post_send_with_config(config_, qp, wr);
}

Result<void> ConfiguredLauncherView::post_recv(const NdsDeviceQp &qp, const NdsDeviceRecvWr &wr) const {
    if (launcher_ == nullptr)
        return Error{ErrorCode::kInvalidArgument, "configured launcher is empty"};
    if (!config_.sync)
        return Error{ErrorCode::kInvalidArgument, "asynchronous launcher calls are not supported"};
    return launcher_->post_recv_with_config(config_, qp, wr);
}

Result<std::uint32_t> ConfiguredLauncherView::poll_cq(const NdsDeviceQp &qp, bool send_cq,
                                                      std::uint32_t max_completions, NdsDeviceWc *completions) const {
    if (launcher_ == nullptr)
        return Error{ErrorCode::kInvalidArgument, "configured launcher is empty"};
    if (!config_.sync)
        return Error{ErrorCode::kInvalidArgument, "asynchronous launcher calls are not supported"};
    return launcher_->poll_cq_with_config(config_, qp, send_cq, max_completions, completions);
}

Result<std::unique_ptr<Launcher>> Launcher::open(Runtime *runtime, BackendMode mode, const std::string &artifact) {
    if (runtime == nullptr || !runtime->initialized())
        return Error{ErrorCode::kInvalidArgument, "launcher open requires an initialized runtime"};
    switch (mode) {
        case BackendMode::Ra:
            return RaLauncher::open(artifact);
        case BackendMode::Aiv:
            return AivLauncher::open(runtime, artifact);
        case BackendMode::Aicpu:
            return AicpuLauncher::open(runtime, artifact);
    }
    return Error{ErrorCode::kInvalidArgument, "backend mode is invalid"};
}

Result<void> Launcher::post_send(const NdsDeviceQp &qp, const NdsDeviceSendWr &wr) const {
    return post_send_with_config({}, qp, wr);
}

Result<void> Launcher::post_recv(const NdsDeviceQp &qp, const NdsDeviceRecvWr &wr) const {
    return post_recv_with_config({}, qp, wr);
}

Result<std::uint32_t> Launcher::poll_cq(const NdsDeviceQp &qp, bool send_cq, std::uint32_t max_completions,
                                        NdsDeviceWc *completions) const {
    return poll_cq_with_config({}, qp, send_cq, max_completions, completions);
}

ConfiguredLauncherView Launcher::with_config(const LaunchConfig &config) const {
    return ConfiguredLauncherView(this, config);
}

}  // namespace nds::client
