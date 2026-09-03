#include "launcher.hh"

#include "loaders/shared_library.hh"
#include "backend_abi.hh"

#include <utility>
#include <chrono>
#include <thread>

namespace nds::client {

class RaLauncher::Impl {
public:
    SharedLibrary library;
    NdsRaBackendPostSend post_send{};
    NdsRaBackendPostRecv post_recv{};
    NdsRaBackendPollCq poll_cq{};
    NdsRaBackendRdmaSend rdma_send{};
    NdsRaBackendRdmaRecv rdma_recv{};
    NdsRaBackendRdmaRead rdma_read{};
    NdsRaBackendRdmaWrite rdma_write{};
    NdsRaBackendStorageBootstrap storage_bootstrap{};
    NdsRaBackendStorageRead storage_read{};
    NdsRaBackendStorageWrite storage_write{};
    NdsRaBackendStorageReadBatch storage_read_batch{};
    NdsRaBackendStorageWriteBatch storage_write_batch{};
    NdsRaBackendStorageWait storage_wait{};
};

RaLauncher::RaLauncher() = default;
RaLauncher::~RaLauncher() = default;

Result<std::unique_ptr<Launcher>> RaLauncher::open(const std::string &backend_path) {
    std::unique_ptr<RaLauncher> launcher = std::make_unique<RaLauncher>();
    NDS_RETURN_IF_ERROR(launcher->load(backend_path));
    return std::unique_ptr<Launcher>(std::move(launcher));
}

Result<void> RaLauncher::load(const std::string &backend_path) {
    if (impl_ != nullptr || backend_path.empty())
        return Error{ErrorCode::kInvalidArgument, impl_ != nullptr ? "NDS RA launcher is already loaded"
                                                                   : "NDS RA requires an NDS backend artifact path"};

    NDS_ASSIGN_OR_RETURN(SharedLibrary library, SharedLibrary::open(backend_path));
    NDS_ASSIGN_OR_RETURN(NdsRaBackendPostSend post_send,
                         library.resolve_required<NdsRaBackendPostSend>("nds_ra_backend_post_send"));
    NDS_ASSIGN_OR_RETURN(NdsRaBackendPostRecv post_recv,
                         library.resolve_required<NdsRaBackendPostRecv>("nds_ra_backend_post_recv"));
    NDS_ASSIGN_OR_RETURN(NdsRaBackendPollCq poll_cq,
                         library.resolve_required<NdsRaBackendPollCq>("nds_ra_backend_poll_cq"));
    NDS_ASSIGN_OR_RETURN(NdsRaBackendRdmaSend rdma_send,
                         library.resolve_required<NdsRaBackendRdmaSend>("nds_ra_backend_rdma_send"));
    NDS_ASSIGN_OR_RETURN(NdsRaBackendRdmaRecv rdma_recv,
                         library.resolve_required<NdsRaBackendRdmaRecv>("nds_ra_backend_rdma_recv"));
    NDS_ASSIGN_OR_RETURN(NdsRaBackendRdmaRead rdma_read,
                         library.resolve_required<NdsRaBackendRdmaRead>("nds_ra_backend_rdma_read"));
    NDS_ASSIGN_OR_RETURN(NdsRaBackendRdmaWrite rdma_write,
                         library.resolve_required<NdsRaBackendRdmaWrite>("nds_ra_backend_rdma_write"));
    NDS_ASSIGN_OR_RETURN(NdsRaBackendStorageBootstrap storage_bootstrap,
                         library.resolve_required<NdsRaBackendStorageBootstrap>("nds_ra_backend_storage_bootstrap"));
    NDS_ASSIGN_OR_RETURN(NdsRaBackendStorageRead storage_read,
                         library.resolve_required<NdsRaBackendStorageRead>("nds_ra_backend_storage_read"));
    NDS_ASSIGN_OR_RETURN(NdsRaBackendStorageWrite storage_write,
                         library.resolve_required<NdsRaBackendStorageWrite>("nds_ra_backend_storage_write"));
    NDS_ASSIGN_OR_RETURN(NdsRaBackendStorageReadBatch storage_read_batch,
                         library.resolve_required<NdsRaBackendStorageReadBatch>("nds_ra_backend_storage_batch_read"));
    NDS_ASSIGN_OR_RETURN(NdsRaBackendStorageWriteBatch storage_write_batch,
                         library.resolve_required<NdsRaBackendStorageWriteBatch>("nds_ra_backend_storage_batch_write"));
    NDS_ASSIGN_OR_RETURN(NdsRaBackendStorageWait storage_wait,
                         library.resolve_required<NdsRaBackendStorageWait>("nds_ra_backend_storage_wait"));
    impl_ = std::make_unique<Impl>();
    impl_->library = std::move(library);
    impl_->post_send = post_send;
    impl_->post_recv = post_recv;
    impl_->poll_cq = poll_cq;
    impl_->rdma_send = rdma_send;
    impl_->rdma_recv = rdma_recv;
    impl_->rdma_read = rdma_read;
    impl_->rdma_write = rdma_write;
    impl_->storage_bootstrap = storage_bootstrap;
    impl_->storage_read = storage_read;
    impl_->storage_write = storage_write;
    impl_->storage_read_batch = storage_read_batch;
    impl_->storage_write_batch = storage_write_batch;
    impl_->storage_wait = storage_wait;
    return {};
}

Result<void> RaLauncher::post_send_with_config(const LaunchConfig &config, const NdsQpDescriptor &qp,
                                               const NdsSendWr &wr) const {
    if (impl_ == nullptr || impl_->post_send == nullptr)
        return Error{ErrorCode::kRuntime, "RA backend is not loaded"};
    return impl_->post_send(&qp, &wr, config.stream) == 0 ? Result<void>{}
                                                          : Error{ErrorCode::kRa, "RA backend Send failed"};
}

Result<void> RaLauncher::post_recv_with_config(const LaunchConfig &, const NdsQpDescriptor &qp,
                                               const NdsRecvWr &wr) const {
    if (impl_ == nullptr || impl_->post_recv == nullptr)
        return Error{ErrorCode::kRuntime, "RA backend is not loaded"};
    return impl_->post_recv(&qp, &wr) == 0 ? Result<void>{} : Error{ErrorCode::kRa, "RA backend receive failed"};
}

Result<std::uint32_t> RaLauncher::poll_cq_with_config(const LaunchConfig &, const NdsQpDescriptor &qp, bool send_cq,
                                                      std::uint32_t max_completions, NdsWc *wc) const {
    if (impl_ == nullptr || impl_->poll_cq == nullptr)
        return Error{ErrorCode::kRuntime, "RA backend is not loaded"};
    const int result = impl_->poll_cq(&qp, send_cq ? 1U : 0U, max_completions, wc);
    return result < 0 ? Result<std::uint32_t>(Error{ErrorCode::kRa, "RA backend CQ poll failed"})
                      : Result<std::uint32_t>(static_cast<std::uint32_t>(result));
}

Result<void> RaLauncher::rdma_send_with_config(const LaunchConfig &, const NdsTransportDescriptor &transport,
                                               std::uint32_t queue_index, const NdsSendWr &wr) const {
    if (impl_ == nullptr || impl_->rdma_send == nullptr)
        return Error{ErrorCode::kRuntime, "RA backend is not loaded"};
    return impl_->rdma_send(&transport, queue_index, &wr) == 0 ? Result<void>{}
                                                               : Error{ErrorCode::kRa, "RA backend RDMA send failed"};
}

Result<void> RaLauncher::rdma_recv_with_config(const LaunchConfig &, const NdsTransportDescriptor &transport,
                                               std::uint32_t queue_index, const NdsRecvWr &wr) const {
    if (impl_ == nullptr || impl_->rdma_recv == nullptr)
        return Error{ErrorCode::kRuntime, "RA backend is not loaded"};
    return impl_->rdma_recv(&transport, queue_index, &wr) == 0
               ? Result<void>{}
               : Error{ErrorCode::kRa, "RA backend RDMA receive failed"};
}

Result<void> RaLauncher::rdma_read_with_config(const LaunchConfig &, const NdsTransportDescriptor &transport,
                                               std::uint32_t queue_index, const NdsSendWr &wr) const {
    if (impl_ == nullptr || impl_->rdma_read == nullptr)
        return Error{ErrorCode::kRuntime, "RA backend is not loaded"};
    return impl_->rdma_read(&transport, queue_index, &wr) == 0 ? Result<void>{}
                                                               : Error{ErrorCode::kRa, "RA backend RDMA read failed"};
}

Result<void> RaLauncher::rdma_write_with_config(const LaunchConfig &, const NdsTransportDescriptor &transport,
                                                std::uint32_t queue_index, const NdsSendWr &wr) const {
    if (impl_ == nullptr || impl_->rdma_write == nullptr)
        return Error{ErrorCode::kRuntime, "RA backend is not loaded"};
    return impl_->rdma_write(&transport, queue_index, &wr) == 0 ? Result<void>{}
                                                                : Error{ErrorCode::kRa, "RA backend RDMA write failed"};
}

Result<void> RaLauncher::storage_bootstrap_with_config(const LaunchConfig &,
                                                       const NdsStorageBootstrapDescriptor &bootstrap) const {
    if (impl_ == nullptr || impl_->storage_bootstrap == nullptr)
        return Error{ErrorCode::kRuntime, "RA backend is not loaded"};
    return impl_->storage_bootstrap(&bootstrap) == 0 ? Result<void>{}
                                                     : Error{ErrorCode::kRa, "RA backend storage bootstrap failed"};
}

Result<void> RaLauncher::storage_read_with_config(const LaunchConfig &, const NdsStorageDescriptor &storage,
                                                  std::uint32_t slot_id, std::uint64_t server_offset,
                                                  std::uint64_t buffer_address, std::uint32_t buffer_key,
                                                  std::uint32_t length) const {
    if (impl_ == nullptr || impl_->storage_read == nullptr)
        return Error{ErrorCode::kRuntime, "RA backend is not loaded"};
    const NdsStorageOperationArgs args{.storage = storage,
                                       .operation = {.slot_id = slot_id,
                                                     .reserved = 0U,
                                                     .server_offset = server_offset,
                                                     .buffer_address = buffer_address,
                                                     .buffer_key = buffer_key,
                                                     .length = length},
                                       .return_value = 0};
    return impl_->storage_read(&args) == 0 ? Result<void>{} : Error{ErrorCode::kRa, "RA backend storage Read failed"};
}

Result<void> RaLauncher::storage_write_with_config(const LaunchConfig &, const NdsStorageDescriptor &storage,
                                                   std::uint32_t slot_id, std::uint64_t server_offset,
                                                   std::uint64_t buffer_address, std::uint32_t buffer_key,
                                                   std::uint32_t length) const {
    if (impl_ == nullptr || impl_->storage_write == nullptr)
        return Error{ErrorCode::kRuntime, "RA backend is not loaded"};
    const NdsStorageOperationArgs args{.storage = storage,
                                       .operation = {.slot_id = slot_id,
                                                     .reserved = 0U,
                                                     .server_offset = server_offset,
                                                     .buffer_address = buffer_address,
                                                     .buffer_key = buffer_key,
                                                     .length = length},
                                       .return_value = 0};
    return impl_->storage_write(&args) == 0 ? Result<void>{} : Error{ErrorCode::kRa, "RA backend storage Write failed"};
}

Result<void> RaLauncher::storage_read_batch_with_config(const LaunchConfig &, const NdsStorageDescriptor &storage,
                                                        std::uint32_t slot_id, std::uint64_t entries_address,
                                                        std::uint32_t entries_key, std::uint32_t entry_count) const {
    if (impl_ == nullptr || impl_->storage_read_batch == nullptr)
        return Error{ErrorCode::kRuntime, "RA backend is not loaded"};
    const NdsStorageBatchOperationArgs args{.storage = storage,
                                            .operation = {.slot_id = slot_id,
                                                          .entry_count = entry_count,
                                                          .entries_address = entries_address,
                                                          .entries_key = entries_key,
                                                          .reserved = 0U},
                                            .return_value = 0};
    return impl_->storage_read_batch(&args) == 0 ? Result<void>{}
                                                 : Error{ErrorCode::kRa, "RA backend storage batch Read failed"};
}

Result<void> RaLauncher::storage_write_batch_with_config(const LaunchConfig &, const NdsStorageDescriptor &storage,
                                                         std::uint32_t slot_id, std::uint64_t entries_address,
                                                         std::uint32_t entries_key, std::uint32_t entry_count) const {
    if (impl_ == nullptr || impl_->storage_write_batch == nullptr)
        return Error{ErrorCode::kRuntime, "RA backend is not loaded"};
    const NdsStorageBatchOperationArgs args{.storage = storage,
                                            .operation = {.slot_id = slot_id,
                                                          .entry_count = entry_count,
                                                          .entries_address = entries_address,
                                                          .entries_key = entries_key,
                                                          .reserved = 0U},
                                            .return_value = 0};
    return impl_->storage_write_batch(&args) == 0 ? Result<void>{}
                                                  : Error{ErrorCode::kRa, "RA backend storage batch Write failed"};
}

Result<void> RaLauncher::storage_wait_with_config(const LaunchConfig &config, const NdsStorageDescriptor &storage,
                                                  std::uint32_t slot_id) const {
    if (impl_ == nullptr || impl_->storage_wait == nullptr)
        return Error{ErrorCode::kRuntime, "RA backend is not loaded"};
    if (config.sync_timeout_ms <= 0)
        return Error{ErrorCode::kInvalidArgument, "storage wait requires a positive timeout"};
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(config.sync_timeout_ms);
    for (;;) {
        const int result = impl_->storage_wait(&storage, slot_id);
        if (result == 0)
            return {};
        if (result != 1 || std::chrono::steady_clock::now() >= deadline)
            return Error{ErrorCode::kProtocol,
                         result == 1 ? "timed out waiting for storage completion" : "RA storage completion failed"};
        std::this_thread::yield();
    }
}

}  // namespace nds::client
