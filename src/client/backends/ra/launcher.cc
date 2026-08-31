#include "launcher.hh"

#include "loaders/shared_library.hh"
#include "backend_abi.hh"

#include <utility>

namespace nds {

namespace {

NdsDeviceQp host_qp_view(client::Runtime *runtime, client::QueuePair *qp) {
    NdsDeviceQp view{};
    view.host_runtime_address = reinterpret_cast<std::uint64_t>(runtime);
    view.host_qp_address = reinterpret_cast<std::uint64_t>(qp);
    return view;
}

NdsDeviceTransport host_transport_view(const RaConnection &connection) {
    static thread_local NdsDeviceQp qp;
    qp = host_qp_view(connection.runtime, connection.qp);
    return {reinterpret_cast<std::uint64_t>(&qp), 1U, 0U};
}

NdsDeviceStorageContext host_storage_view(const RaStorageContext &context) {
    NdsDeviceStorageContext view{};
    view.transport = host_transport_view(context.connection);
    view.command_buffer = {context.command_buffer.address, context.command_buffer.length,
                           context.command_buffer.local_key};
    view.completion = {context.completion.address, context.completion.length, context.completion.local_key};
    view.capacity = context.capacity;
    return view;
}

}  // namespace

class RaLauncher::Impl {
public:
    client::SharedLibrary library;
    NdsRaBackendPostSend post_send{};
    NdsRaBackendPostRecv post_recv{};
    NdsRaBackendPollCq poll_cq{};
    NdsRaBackendRdmaSend rdma_send{}, rdma_read{}, rdma_write{};
    NdsRaBackendRdmaRecv rdma_recv{};
    NdsRaBackendStorage storage_read{};
    NdsRaBackendStorageWrite storage_write{};
    NdsRaBackendStorageBatchRead storage_batch_read{};
    NdsRaBackendStorageBatchWrite storage_batch_write{};
};

RaLauncher::~RaLauncher() = default;

Result<void> RaLauncher::load(const std::string &backend_path) {
    if (impl_ != nullptr || backend_path.empty())
        return Error{ErrorCode::kInvalidArgument, impl_ != nullptr ? "NDS RA launcher is already loaded"
                                                                   : "NDS RA requires an NDS backend artifact path"};

    NDS_ASSIGN_OR_RETURN(client::SharedLibrary library, client::SharedLibrary::open(backend_path));
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
    NDS_ASSIGN_OR_RETURN(NdsRaBackendRdmaSend rdma_read,
                         library.resolve_required<NdsRaBackendRdmaSend>("nds_ra_backend_rdma_read"));
    NDS_ASSIGN_OR_RETURN(NdsRaBackendRdmaSend rdma_write,
                         library.resolve_required<NdsRaBackendRdmaSend>("nds_ra_backend_rdma_write"));
    NDS_ASSIGN_OR_RETURN(NdsRaBackendStorage storage_read,
                         library.resolve_required<NdsRaBackendStorage>("nds_ra_backend_storage_read"));
    NDS_ASSIGN_OR_RETURN(NdsRaBackendStorageWrite storage_write,
                         library.resolve_required<NdsRaBackendStorageWrite>("nds_ra_backend_storage_write"));
    NDS_ASSIGN_OR_RETURN(NdsRaBackendStorageBatchRead storage_batch_read,
                         library.resolve_required<NdsRaBackendStorageBatchRead>("nds_ra_backend_storage_batch_read"));
    NDS_ASSIGN_OR_RETURN(NdsRaBackendStorageBatchWrite storage_batch_write,
                         library.resolve_required<NdsRaBackendStorageBatchWrite>("nds_ra_backend_storage_batch_write"));
    impl_ = std::make_unique<Impl>();
    impl_->library = std::move(library);
    impl_->post_send = post_send;
    impl_->post_recv = post_recv;
    impl_->poll_cq = poll_cq;
    impl_->rdma_send = rdma_send;
    impl_->rdma_recv = rdma_recv;
    impl_->rdma_read = rdma_read;
    impl_->rdma_write = rdma_write;
    impl_->storage_read = storage_read;
    impl_->storage_write = storage_write;
    impl_->storage_batch_read = storage_batch_read;
    impl_->storage_batch_write = storage_batch_write;
    return {};
}

Result<void> RaLauncher::post_send(const NdsDeviceQp &qp, const NdsDeviceSendWr &wr, void *stream) {
    if (impl_ == nullptr || impl_->post_send == nullptr)
        return Error{ErrorCode::kRuntime, "RA backend is not loaded"};
    return impl_->post_send(&qp, &wr, stream) == 0 ? Result<void>{} : Error{ErrorCode::kRa, "RA backend Send failed"};
}

Result<void> RaLauncher::post_recv(const NdsDeviceQp &qp, const NdsDeviceRecvWr &wr) {
    if (impl_ == nullptr || impl_->post_recv == nullptr)
        return Error{ErrorCode::kRuntime, "RA backend is not loaded"};
    return impl_->post_recv(&qp, &wr) == 0 ? Result<void>{} : Error{ErrorCode::kRa, "RA backend receive failed"};
}

Result<std::uint32_t> RaLauncher::poll_cq(const NdsDeviceQp &qp, std::uint32_t send_cq, std::uint32_t max_completions,
                                          NdsDeviceWc *wc) {
    if (impl_ == nullptr || impl_->poll_cq == nullptr)
        return Error{ErrorCode::kRuntime, "RA backend is not loaded"};
    const int result = impl_->poll_cq(&qp, send_cq, max_completions, wc);
    return result < 0 ? Result<std::uint32_t>(Error{ErrorCode::kRa, "RA backend CQ poll failed"})
                      : Result<std::uint32_t>(static_cast<std::uint32_t>(result));
}

Result<void> RaLauncher::rdma_send(const NdsDeviceTransport &transport, const NdsDeviceSendWr &wr) {
    if (impl_ == nullptr || impl_->rdma_send == nullptr)
        return Error{ErrorCode::kRuntime, "RA backend is not loaded"};
    return impl_->rdma_send(&transport, &wr) == 0 ? Result<void>{}
                                                  : Error{ErrorCode::kRa, "RA backend RDMA send failed"};
}

Result<void> RaLauncher::rdma_send(const RaConnection &connection, const NdsDeviceSendWr &wr) {
    return rdma_send(host_transport_view(connection), wr);
}

Result<void> RaLauncher::rdma_recv(const NdsDeviceTransport &transport, const NdsDeviceRecvWr &wr) {
    if (impl_ == nullptr || impl_->rdma_recv == nullptr)
        return Error{ErrorCode::kRuntime, "RA backend is not loaded"};
    return impl_->rdma_recv(&transport, &wr) == 0 ? Result<void>{}
                                                  : Error{ErrorCode::kRa, "RA backend RDMA receive failed"};
}

Result<void> RaLauncher::rdma_recv(const RaConnection &connection, const NdsDeviceRecvWr &wr) {
    return rdma_recv(host_transport_view(connection), wr);
}

Result<void> RaLauncher::rdma_read(const NdsDeviceTransport &transport, const NdsDeviceSendWr &wr) {
    if (impl_ == nullptr || impl_->rdma_read == nullptr)
        return Error{ErrorCode::kRuntime, "RA backend is not loaded"};
    return impl_->rdma_read(&transport, &wr) == 0 ? Result<void>{}
                                                  : Error{ErrorCode::kRa, "RA backend RDMA read failed"};
}

Result<void> RaLauncher::rdma_read(const RaConnection &connection, const NdsDeviceSendWr &wr) {
    return rdma_read(host_transport_view(connection), wr);
}

Result<void> RaLauncher::rdma_write(const NdsDeviceTransport &transport, const NdsDeviceSendWr &wr) {
    if (impl_ == nullptr || impl_->rdma_write == nullptr)
        return Error{ErrorCode::kRuntime, "RA backend is not loaded"};
    return impl_->rdma_write(&transport, &wr) == 0 ? Result<void>{}
                                                   : Error{ErrorCode::kRa, "RA backend RDMA write failed"};
}

Result<void> RaLauncher::rdma_write(const RaConnection &connection, const NdsDeviceSendWr &wr) {
    return rdma_write(host_transport_view(connection), wr);
}

Result<void> RaLauncher::storage_read(const NdsDeviceStorageContext &context, const StorageReadCommand &command) {
    if (impl_ == nullptr || impl_->storage_read == nullptr)
        return Error{ErrorCode::kRuntime, "RA backend is not loaded"};
    return impl_->storage_read(&context, &command) == 0 ? Result<void>{}
                                                        : Error{ErrorCode::kRa, "RA backend storage read failed"};
}

Result<void> RaLauncher::storage_read(const RaStorageContext &context, const StorageReadCommand &command) {
    return storage_read(host_storage_view(context), command);
}

Result<void> RaLauncher::storage_write(const NdsDeviceStorageContext &context, const StorageWriteCommand &command) {
    if (impl_ == nullptr || impl_->storage_write == nullptr)
        return Error{ErrorCode::kRuntime, "RA backend is not loaded"};
    return impl_->storage_write(&context, &command) == 0 ? Result<void>{}
                                                         : Error{ErrorCode::kRa, "RA backend storage write failed"};
}

Result<void> RaLauncher::storage_write(const RaStorageContext &context, const StorageWriteCommand &command) {
    return storage_write(host_storage_view(context), command);
}

Result<void> RaLauncher::storage_batch_read(const NdsDeviceStorageContext &context,
                                            const StorageBatchReadCommand &command) {
    if (impl_ == nullptr || impl_->storage_batch_read == nullptr)
        return Error{ErrorCode::kRuntime, "RA backend is not loaded"};
    return impl_->storage_batch_read(&context, &command) == 0
               ? Result<void>{}
               : Error{ErrorCode::kRa, "RA backend batch storage read failed"};
}

Result<void> RaLauncher::storage_batch_read(const RaStorageContext &context, const StorageBatchReadCommand &command) {
    return storage_batch_read(host_storage_view(context), command);
}

Result<void> RaLauncher::storage_batch_write(const NdsDeviceStorageContext &context,
                                             const StorageBatchWriteCommand &command) {
    if (impl_ == nullptr || impl_->storage_batch_write == nullptr)
        return Error{ErrorCode::kRuntime, "RA backend is not loaded"};
    return impl_->storage_batch_write(&context, &command) == 0
               ? Result<void>{}
               : Error{ErrorCode::kRa, "RA backend batch storage write failed"};
}

Result<void> RaLauncher::storage_batch_write(const RaStorageContext &context, const StorageBatchWriteCommand &command) {
    return storage_batch_write(host_storage_view(context), command);
}

}  // namespace nds
