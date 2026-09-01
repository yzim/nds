#include "launcher.hh"

#include "aicpu/launcher.hh"
#include "aiv/launcher.hh"
#include "ra/launcher.hh"
#include "runtime.hh"

namespace nds::client {

ConfiguredLauncherView::ConfiguredLauncherView(const Launcher *launcher, const LaunchConfig &config)
    : launcher_(launcher), config_(config) {}

Result<void> ConfiguredLauncherView::post_send(const NdsQpDescriptor &qp, const NdsSendWr &wr) const {
    if (launcher_ == nullptr)
        return Error{ErrorCode::kInvalidArgument, "configured launcher is empty"};
    if (!config_.sync)
        return Error{ErrorCode::kInvalidArgument, "asynchronous launcher calls are not supported"};
    return launcher_->post_send_with_config(config_, qp, wr);
}

Result<PostSendBatchResult> ConfiguredLauncherView::post_send_batch(const NdsQpDescriptor &qp,
                                                                    std::span<const NdsSendWr> wrs) const {
    if (launcher_ == nullptr)
        return Error{ErrorCode::kInvalidArgument, "configured launcher is empty"};
    if (!config_.sync)
        return Error{ErrorCode::kInvalidArgument, "asynchronous launcher calls are not supported"};
    return launcher_->post_send_batch_with_config(config_, qp, wrs);
}

Result<void> ConfiguredLauncherView::post_recv(const NdsQpDescriptor &qp, const NdsRecvWr &wr) const {
    if (launcher_ == nullptr)
        return Error{ErrorCode::kInvalidArgument, "configured launcher is empty"};
    if (!config_.sync)
        return Error{ErrorCode::kInvalidArgument, "asynchronous launcher calls are not supported"};
    return launcher_->post_recv_with_config(config_, qp, wr);
}

Result<std::uint32_t> ConfiguredLauncherView::poll_cq(const NdsQpDescriptor &qp, bool send_cq,
                                                      std::uint32_t max_completions, NdsWc *completions) const {
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

Result<void> Launcher::post_send(const NdsQpDescriptor &qp, const NdsSendWr &wr) const {
    return post_send_with_config({}, qp, wr);
}

Result<PostSendBatchResult> Launcher::post_send_batch(const NdsQpDescriptor &qp, std::span<const NdsSendWr> wrs) const {
    return post_send_batch_with_config({}, qp, wrs);
}

Result<void> Launcher::post_recv(const NdsQpDescriptor &qp, const NdsRecvWr &wr) const {
    return post_recv_with_config({}, qp, wr);
}

Result<std::uint32_t> Launcher::poll_cq(const NdsQpDescriptor &qp, bool send_cq, std::uint32_t max_completions,
                                        NdsWc *completions) const {
    return poll_cq_with_config({}, qp, send_cq, max_completions, completions);
}

Result<void> Launcher::rdma_send(const LaunchConfig &config, const NdsTransportDescriptor &transport,
                                 std::uint32_t queue_index, const NdsSendWr &wr) const {
    return rdma_send_with_config(config, transport, queue_index, wr);
}

Result<void> Launcher::rdma_recv(const LaunchConfig &config, const NdsTransportDescriptor &transport,
                                 std::uint32_t queue_index, const NdsRecvWr &wr) const {
    return rdma_recv_with_config(config, transport, queue_index, wr);
}

Result<void> Launcher::rdma_read(const LaunchConfig &config, const NdsTransportDescriptor &transport,
                                 std::uint32_t queue_index, const NdsSendWr &wr) const {
    return rdma_read_with_config(config, transport, queue_index, wr);
}

Result<void> Launcher::rdma_write(const LaunchConfig &config, const NdsTransportDescriptor &transport,
                                  std::uint32_t queue_index, const NdsSendWr &wr) const {
    return rdma_write_with_config(config, transport, queue_index, wr);
}

Result<PostSendBatchResult> Launcher::rdma_send_batch(const LaunchConfig &config,
                                                      const NdsTransportDescriptor &transport,
                                                      std::uint32_t queue_index, std::span<const NdsSendWr> wrs) const {
    return rdma_send_batch_with_config(config, transport, queue_index, wrs);
}

ConfiguredLauncherView Launcher::with_config(const LaunchConfig &config) const {
    return ConfiguredLauncherView(this, config);
}

Result<PostSendBatchResult> Launcher::post_send_batch_with_config(const LaunchConfig &, const NdsQpDescriptor &,
                                                                  std::span<const NdsSendWr>) const {
    return Error{ErrorCode::kUnsupported, "backend does not support transport send batches"};
}

Result<PostSendBatchResult> Launcher::rdma_send_batch_with_config(const LaunchConfig &, const NdsTransportDescriptor &,
                                                                  std::uint32_t, std::span<const NdsSendWr>) const {
    return Error{ErrorCode::kUnsupported, "backend does not support transport send batches"};
}

}  // namespace nds::client
