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

Result<void> ConfiguredLauncherView::rdma_send(const NdsTransportDescriptor &transport, std::uint32_t queue_index,
                                               const NdsSendWr &wr) const {
    if (launcher_ == nullptr)
        return Error{ErrorCode::kInvalidArgument, "configured launcher is empty"};
    if (!config_.sync)
        return Error{ErrorCode::kInvalidArgument, "asynchronous launcher calls are not supported"};
    return launcher_->rdma_send_with_config(config_, transport, queue_index, wr);
}

Result<void> ConfiguredLauncherView::rdma_recv(const NdsTransportDescriptor &transport, std::uint32_t queue_index,
                                               const NdsRecvWr &wr) const {
    if (launcher_ == nullptr)
        return Error{ErrorCode::kInvalidArgument, "configured launcher is empty"};
    if (!config_.sync)
        return Error{ErrorCode::kInvalidArgument, "asynchronous launcher calls are not supported"};
    return launcher_->rdma_recv_with_config(config_, transport, queue_index, wr);
}

Result<void> ConfiguredLauncherView::rdma_read(const NdsTransportDescriptor &transport, std::uint32_t queue_index,
                                               const NdsSendWr &wr) const {
    if (launcher_ == nullptr)
        return Error{ErrorCode::kInvalidArgument, "configured launcher is empty"};
    if (!config_.sync)
        return Error{ErrorCode::kInvalidArgument, "asynchronous launcher calls are not supported"};
    return launcher_->rdma_read_with_config(config_, transport, queue_index, wr);
}

Result<void> ConfiguredLauncherView::rdma_write(const NdsTransportDescriptor &transport, std::uint32_t queue_index,
                                                const NdsSendWr &wr) const {
    if (launcher_ == nullptr)
        return Error{ErrorCode::kInvalidArgument, "configured launcher is empty"};
    if (!config_.sync)
        return Error{ErrorCode::kInvalidArgument, "asynchronous launcher calls are not supported"};
    return launcher_->rdma_write_with_config(config_, transport, queue_index, wr);
}

Result<PostSendBatchResult> ConfiguredLauncherView::rdma_send_batch(const NdsTransportDescriptor &transport,
                                                                    std::uint32_t queue_index,
                                                                    std::span<const NdsSendWr> wrs) const {
    if (launcher_ == nullptr)
        return Error{ErrorCode::kInvalidArgument, "configured launcher is empty"};
    if (!config_.sync)
        return Error{ErrorCode::kInvalidArgument, "asynchronous launcher calls are not supported"};
    return launcher_->rdma_send_batch_with_config(config_, transport, queue_index, wrs);
}

Result<void> ConfiguredLauncherView::storage_bootstrap(const NdsStorageBootstrapDescriptor &bootstrap) const {
    if (launcher_ == nullptr)
        return Error{ErrorCode::kInvalidArgument, "configured launcher is empty"};
    if (!config_.sync)
        return Error{ErrorCode::kInvalidArgument, "asynchronous launcher calls are not supported"};
    return launcher_->storage_bootstrap_with_config(config_, bootstrap);
}

Result<void> ConfiguredLauncherView::storage_read(const NdsStorageDescriptor &storage, std::uint32_t slot_id,
                                                  std::uint64_t server_offset, std::uint64_t buffer_address,
                                                  std::uint32_t buffer_key, std::uint32_t length) const {
    if (launcher_ == nullptr)
        return Error{ErrorCode::kInvalidArgument, "configured launcher is empty"};
    if (!config_.sync)
        return Error{ErrorCode::kInvalidArgument, "asynchronous launcher calls are not supported"};
    return launcher_->storage_read_with_config(config_, storage, slot_id, server_offset, buffer_address, buffer_key,
                                               length);
}

Result<void> ConfiguredLauncherView::storage_write(const NdsStorageDescriptor &storage, std::uint32_t slot_id,
                                                   std::uint64_t server_offset, std::uint64_t buffer_address,
                                                   std::uint32_t buffer_key, std::uint32_t length) const {
    if (launcher_ == nullptr)
        return Error{ErrorCode::kInvalidArgument, "configured launcher is empty"};
    if (!config_.sync)
        return Error{ErrorCode::kInvalidArgument, "asynchronous launcher calls are not supported"};
    return launcher_->storage_write_with_config(config_, storage, slot_id, server_offset, buffer_address, buffer_key,
                                                length);
}

Result<void> ConfiguredLauncherView::storage_read_batch(const NdsStorageDescriptor &storage, std::uint32_t slot_id,
                                                        std::uint64_t entries_address, std::uint32_t entries_key,
                                                        std::uint32_t entry_count) const {
    if (launcher_ == nullptr)
        return Error{ErrorCode::kInvalidArgument, "configured launcher is empty"};
    if (!config_.sync)
        return Error{ErrorCode::kInvalidArgument, "asynchronous launcher calls are not supported"};
    return launcher_->storage_read_batch_with_config(config_, storage, slot_id, entries_address, entries_key,
                                                     entry_count);
}

Result<void> ConfiguredLauncherView::storage_write_batch(const NdsStorageDescriptor &storage, std::uint32_t slot_id,
                                                         std::uint64_t entries_address, std::uint32_t entries_key,
                                                         std::uint32_t entry_count) const {
    if (launcher_ == nullptr)
        return Error{ErrorCode::kInvalidArgument, "configured launcher is empty"};
    if (!config_.sync)
        return Error{ErrorCode::kInvalidArgument, "asynchronous launcher calls are not supported"};
    return launcher_->storage_write_batch_with_config(config_, storage, slot_id, entries_address, entries_key,
                                                      entry_count);
}

Result<void> ConfiguredLauncherView::storage_wait(const NdsStorageDescriptor &storage, std::uint32_t slot_id) const {
    if (launcher_ == nullptr)
        return Error{ErrorCode::kInvalidArgument, "configured launcher is empty"};
    if (!config_.sync)
        return Error{ErrorCode::kInvalidArgument, "asynchronous launcher calls are not supported"};
    return launcher_->storage_wait_with_config(config_, storage, slot_id);
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

Result<void> Launcher::storage_bootstrap(const LaunchConfig &config,
                                         const NdsStorageBootstrapDescriptor &bootstrap) const {
    return storage_bootstrap_with_config(config, bootstrap);
}

Result<void> Launcher::storage_read(const LaunchConfig &config, const NdsStorageDescriptor &storage,
                                    std::uint32_t slot_id, std::uint64_t server_offset, std::uint64_t buffer_address,
                                    std::uint32_t buffer_key, std::uint32_t length) const {
    return storage_read_with_config(config, storage, slot_id, server_offset, buffer_address, buffer_key, length);
}

Result<void> Launcher::storage_write(const LaunchConfig &config, const NdsStorageDescriptor &storage,
                                     std::uint32_t slot_id, std::uint64_t server_offset, std::uint64_t buffer_address,
                                     std::uint32_t buffer_key, std::uint32_t length) const {
    return storage_write_with_config(config, storage, slot_id, server_offset, buffer_address, buffer_key, length);
}

Result<void> Launcher::storage_read_batch(const LaunchConfig &config, const NdsStorageDescriptor &storage,
                                          std::uint32_t slot_id, std::uint64_t entries_address,
                                          std::uint32_t entries_key, std::uint32_t entry_count) const {
    return storage_read_batch_with_config(config, storage, slot_id, entries_address, entries_key, entry_count);
}

Result<void> Launcher::storage_write_batch(const LaunchConfig &config, const NdsStorageDescriptor &storage,
                                           std::uint32_t slot_id, std::uint64_t entries_address,
                                           std::uint32_t entries_key, std::uint32_t entry_count) const {
    return storage_write_batch_with_config(config, storage, slot_id, entries_address, entries_key, entry_count);
}

Result<void> Launcher::storage_wait(const LaunchConfig &config, const NdsStorageDescriptor &storage,
                                    std::uint32_t slot_id) const {
    return storage_wait_with_config(config, storage, slot_id);
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
