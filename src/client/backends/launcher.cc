#include "launcher.hh"

#include "loaders/shared_library.hh"
#include "ra/backend_abi.hh"

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
    NdsDeviceTransport view{};
    view.control_qp = host_qp_view(connection.runtime, connection.qp);
    return view;
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
        return unexpected(ErrorCode::kInvalidArgument, impl_ != nullptr
                                                           ? "NDS RA launcher is already loaded"
                                                           : "NDS RA requires an NDS backend artifact path");

    auto library = client::SharedLibrary::open(backend_path);
    if (!library)
        return unexpected(library.error());
    auto post_send = library->resolve_required<NdsRaBackendPostSend>("nds_ra_backend_post_send");
    if (!post_send)
        return unexpected(post_send.error());
    auto poll_cq = library->resolve_required<NdsRaBackendPollCq>("nds_ra_backend_poll_cq");
    if (!poll_cq)
        return unexpected(poll_cq.error());
    auto rdma_send = library->resolve_required<NdsRaBackendRdmaSend>("nds_ra_backend_rdma_send");
    if (!rdma_send)
        return unexpected(rdma_send.error());
    auto rdma_recv = library->resolve_required<NdsRaBackendRdmaRecv>("nds_ra_backend_rdma_recv");
    if (!rdma_recv)
        return unexpected(rdma_recv.error());
    auto rdma_read = library->resolve_required<NdsRaBackendRdmaSend>("nds_ra_backend_rdma_read");
    if (!rdma_read)
        return unexpected(rdma_read.error());
    auto rdma_write = library->resolve_required<NdsRaBackendRdmaSend>("nds_ra_backend_rdma_write");
    if (!rdma_write)
        return unexpected(rdma_write.error());
    auto storage_read = library->resolve_required<NdsRaBackendStorage>("nds_ra_backend_storage_read");
    if (!storage_read)
        return unexpected(storage_read.error());
    auto storage_write = library->resolve_required<NdsRaBackendStorageWrite>("nds_ra_backend_storage_write");
    if (!storage_write)
        return unexpected(storage_write.error());
    auto storage_batch_read =
        library->resolve_required<NdsRaBackendStorageBatchRead>("nds_ra_backend_storage_batch_read");
    if (!storage_batch_read)
        return unexpected(storage_batch_read.error());
    auto storage_batch_write =
        library->resolve_required<NdsRaBackendStorageBatchWrite>("nds_ra_backend_storage_batch_write");
    if (!storage_batch_write)
        return unexpected(storage_batch_write.error());
    impl_ = std::make_unique<Impl>();
    impl_->library = std::move(*library);
    impl_->post_send = *post_send;
    impl_->poll_cq = *poll_cq;
    impl_->rdma_send = *rdma_send;
    impl_->rdma_recv = *rdma_recv;
    impl_->rdma_read = *rdma_read;
    impl_->rdma_write = *rdma_write;
    impl_->storage_read = *storage_read;
    impl_->storage_write = *storage_write;
    impl_->storage_batch_read = *storage_batch_read;
    impl_->storage_batch_write = *storage_batch_write;
    return {};
}

Result<void> RaLauncher::post_send(const NdsDeviceQp &qp, const NdsDeviceSendWr &wr) {
    if (impl_ == nullptr || impl_->post_send == nullptr)
        return unexpected(ErrorCode::kRuntime, "RA backend is not loaded");
    return impl_->post_send(&qp, &wr) == 0 ? Result<void>{} : unexpected(ErrorCode::kRa, "RA backend Send failed");
}

Result<void> RaLauncher::post_send(client::Runtime *runtime, client::QueuePair *qp, const NdsDeviceSendWr &wr) {
    return post_send(host_qp_view(runtime, qp), wr);
}

Result<std::uint32_t> RaLauncher::poll_cq(const NdsDeviceQp &qp, std::uint32_t send_cq, std::uint32_t max_completions,
                                          NdsDeviceWc *wc) {
    if (impl_ == nullptr || impl_->poll_cq == nullptr)
        return unexpected(ErrorCode::kRuntime, "RA backend is not loaded");
    const int result = impl_->poll_cq(&qp, send_cq, max_completions, wc);
    return result < 0 ? Result<std::uint32_t>(unexpected(ErrorCode::kRa, "RA backend CQ poll failed"))
                      : Result<std::uint32_t>(static_cast<std::uint32_t>(result));
}

Result<std::uint32_t> RaLauncher::poll_cq(client::QueuePair *qp, bool send_cq, std::uint32_t max_completions,
                                          NdsDeviceWc *wc) {
    return poll_cq(host_qp_view(nullptr, qp), send_cq, max_completions, wc);
}

Result<void> RaLauncher::rdma_send(const NdsDeviceTransport &transport, const NdsDeviceSendWr &wr) {
    if (impl_ == nullptr || impl_->rdma_send == nullptr)
        return unexpected(ErrorCode::kRuntime, "RA backend is not loaded");
    return impl_->rdma_send(&transport, &wr) == 0 ? Result<void>{}
                                                  : unexpected(ErrorCode::kRa, "RA backend RDMA send failed");
}

Result<void> RaLauncher::rdma_send(const RaConnection &connection, const NdsDeviceSendWr &wr) {
    return rdma_send(host_transport_view(connection), wr);
}

Result<void> RaLauncher::rdma_recv(const NdsDeviceTransport &transport, const NdsDeviceRecvWr &wr) {
    if (impl_ == nullptr || impl_->rdma_recv == nullptr)
        return unexpected(ErrorCode::kRuntime, "RA backend is not loaded");
    return impl_->rdma_recv(&transport, &wr) == 0 ? Result<void>{}
                                                  : unexpected(ErrorCode::kRa, "RA backend RDMA receive failed");
}

Result<void> RaLauncher::rdma_recv(const RaConnection &connection, const NdsDeviceRecvWr &wr) {
    return rdma_recv(host_transport_view(connection), wr);
}

Result<void> RaLauncher::rdma_read(const NdsDeviceTransport &transport, const NdsDeviceSendWr &wr) {
    if (impl_ == nullptr || impl_->rdma_read == nullptr)
        return unexpected(ErrorCode::kRuntime, "RA backend is not loaded");
    return impl_->rdma_read(&transport, &wr) == 0 ? Result<void>{}
                                                  : unexpected(ErrorCode::kRa, "RA backend RDMA read failed");
}

Result<void> RaLauncher::rdma_read(const RaConnection &connection, const NdsDeviceSendWr &wr) {
    return rdma_read(host_transport_view(connection), wr);
}

Result<void> RaLauncher::rdma_write(const NdsDeviceTransport &transport, const NdsDeviceSendWr &wr) {
    if (impl_ == nullptr || impl_->rdma_write == nullptr)
        return unexpected(ErrorCode::kRuntime, "RA backend is not loaded");
    return impl_->rdma_write(&transport, &wr) == 0 ? Result<void>{}
                                                   : unexpected(ErrorCode::kRa, "RA backend RDMA write failed");
}

Result<void> RaLauncher::rdma_write(const RaConnection &connection, const NdsDeviceSendWr &wr) {
    return rdma_write(host_transport_view(connection), wr);
}

Result<void> RaLauncher::storage_read(const NdsDeviceStorageContext &context, const StorageReadCommand &command) {
    if (impl_ == nullptr || impl_->storage_read == nullptr)
        return unexpected(ErrorCode::kRuntime, "RA backend is not loaded");
    return impl_->storage_read(&context, &command) == 0 ? Result<void>{}
                                                        : unexpected(ErrorCode::kRa, "RA backend storage read failed");
}

Result<void> RaLauncher::storage_read(const RaStorageContext &context, const StorageReadCommand &command) {
    return storage_read(host_storage_view(context), command);
}

Result<void> RaLauncher::storage_write(const NdsDeviceStorageContext &context, const StorageWriteCommand &command) {
    if (impl_ == nullptr || impl_->storage_write == nullptr)
        return unexpected(ErrorCode::kRuntime, "RA backend is not loaded");
    return impl_->storage_write(&context, &command) == 0
               ? Result<void>{}
               : unexpected(ErrorCode::kRa, "RA backend storage write failed");
}

Result<void> RaLauncher::storage_write(const RaStorageContext &context, const StorageWriteCommand &command) {
    return storage_write(host_storage_view(context), command);
}

Result<void> RaLauncher::storage_batch_read(const NdsDeviceStorageContext &context,
                                            const StorageBatchReadCommand &command) {
    if (impl_ == nullptr || impl_->storage_batch_read == nullptr)
        return unexpected(ErrorCode::kRuntime, "RA backend is not loaded");
    return impl_->storage_batch_read(&context, &command) == 0
               ? Result<void>{}
               : unexpected(ErrorCode::kRa, "RA backend batch storage read failed");
}

Result<void> RaLauncher::storage_batch_read(const RaStorageContext &context, const StorageBatchReadCommand &command) {
    return storage_batch_read(host_storage_view(context), command);
}

Result<void> RaLauncher::storage_batch_write(const NdsDeviceStorageContext &context,
                                             const StorageBatchWriteCommand &command) {
    if (impl_ == nullptr || impl_->storage_batch_write == nullptr)
        return unexpected(ErrorCode::kRuntime, "RA backend is not loaded");
    return impl_->storage_batch_write(&context, &command) == 0
               ? Result<void>{}
               : unexpected(ErrorCode::kRa, "RA backend batch storage write failed");
}

Result<void> RaLauncher::storage_batch_write(const RaStorageContext &context, const StorageBatchWriteCommand &command) {
    return storage_batch_write(host_storage_view(context), command);
}

AivLauncher::~AivLauncher() {
    reset();
}

Result<void> AivLauncher::load(const std::string &kernel_path) {
    aclrtBinaryLoadOption option{};
    aclrtBinaryLoadOptions options{};
    if (loaded() || kernel_path.empty())
        return unexpected(ErrorCode::kInvalidArgument, loaded() ? "NDS AIV launcher is already loaded"
                                                                : "NDS AIV requires an NDS-built kernel binary path");
    option.type = ACL_RT_BINARY_LOAD_OPT_LAZY_LOAD;
    option.value.isLazyLoad = 1U;
    options.options = &option;
    options.numOpt = 1U;
    const int load_result = aclrtBinaryLoadFromFile(kernel_path.c_str(), &options, &binary_);
    if (load_result != ACL_SUCCESS || binary_ == nullptr) {
        reset();
        return unexpected(ErrorCode::kRuntime,
                          "aclrtBinaryLoadFromFile(NDS AIV binary) failed: " + std::to_string(load_result));
    }
    const int stream_result = aclrtCreateStream(&stream_);
    if (stream_result != ACL_SUCCESS || stream_ == nullptr) {
        reset();
        return unexpected(ErrorCode::kRuntime,
                          "aclrtCreateStream for NDS AIV launch failed: " + std::to_string(stream_result));
    }
    return {};
}

Result<void> AivLauncher::launch_and_wait(const char *kernel_name, void *arguments, std::size_t argument_size,
                                          std::int32_t completion_timeout_ms) {
    if (!loaded() || kernel_name == nullptr || arguments == nullptr || argument_size == 0U ||
        completion_timeout_ms <= 0)
        return unexpected(ErrorCode::kInvalidArgument, "invalid AIV launch arguments");
    const auto [entry, inserted] = functions_.try_emplace(kernel_name, nullptr);
    if (inserted) {
        const int result = aclrtBinaryGetFunction(binary_, kernel_name, &entry->second);
        if (result != ACL_SUCCESS || entry->second == nullptr) {
            functions_.erase(entry);
            return unexpected(ErrorCode::kRuntime, "AIV kernel entry lookup failed: " + std::string(kernel_name));
        }
    }
    aclrtLaunchKernelAttr attributes[2]{};
    attributes[0].id = ACL_RT_LAUNCH_KERNEL_ATTR_SCHEM_MODE;
    attributes[0].value.schemMode = 1U;
    attributes[1].id = ACL_RT_LAUNCH_KERNEL_ATTR_ENGINE_TYPE;
    attributes[1].value.engineType = ACL_RT_ENGINE_TYPE_AIV;
    aclrtLaunchKernelCfg config{attributes, 2U};
    const int launch_result =
        aclrtLaunchKernelWithHostArgs(entry->second, 1U, stream_, &config, arguments, argument_size, nullptr, 0U);
    if (launch_result != ACL_SUCCESS)
        return unexpected(ErrorCode::kRuntime, "AIV kernel launch failed: " + std::string(kernel_name));
    const int sync_result = aclrtSynchronizeStreamWithTimeout(stream_, completion_timeout_ms);
    if (sync_result != ACL_SUCCESS)
        return unexpected(ErrorCode::kRuntime, "AIV kernel synchronization failed: " + std::string(kernel_name));
    return {};
}

void AivLauncher::reset() noexcept {
    if (stream_ != nullptr)
        (void)aclrtDestroyStream(stream_);
    stream_ = nullptr;
    functions_.clear();
    if (binary_ != nullptr)
        (void)aclrtBinaryUnLoad(binary_);
    binary_ = nullptr;
}

bool AivLauncher::loaded() const noexcept {
    return binary_ != nullptr && stream_ != nullptr;
}

AicpuLauncher::~AicpuLauncher() {
    reset();
}

Result<void> AicpuLauncher::load(const std::string &kernel_path) {
    aclrtBinaryLoadOption option{};
    aclrtBinaryLoadOptions options{};
    if (loaded() || kernel_path.empty())
        return unexpected(ErrorCode::kInvalidArgument, loaded() ? "NDS AICPU launcher is already loaded"
                                                                : "NDS AICPU requires an NDS kernel artifact path");
    option.type = ACL_RT_BINARY_LOAD_OPT_CPU_KERNEL_MODE;
    option.value.cpuKernelMode = 0;
    options.options = &option;
    options.numOpt = 1U;
    const int load_result = aclrtBinaryLoadFromFile(kernel_path.c_str(), &options, &binary_);
    if (load_result != ACL_SUCCESS || binary_ == nullptr) {
        reset();
        return unexpected(ErrorCode::kRuntime,
                          "aclrtBinaryLoadFromFile(AICPU kernel artifact) failed: " + std::to_string(load_result));
    }
    const int stream_result = aclrtCreateStreamWithConfig(&stream_, 0U, ACL_STREAM_FAST_LAUNCH | ACL_STREAM_FAST_SYNC);
    if (stream_result != ACL_SUCCESS || stream_ == nullptr) {
        reset();
        return unexpected(ErrorCode::kRuntime, "AICPU launch stream creation failed: " + std::to_string(stream_result));
    }
    return {};
}

Result<void> AicpuLauncher::launch_and_wait(const char *kernel_name, std::uint64_t args_gm_addr,
                                            std::int32_t completion_timeout_ms) {
    if (!loaded() || kernel_name == nullptr || args_gm_addr == 0U || completion_timeout_ms <= 0)
        return unexpected(ErrorCode::kInvalidArgument, "invalid AICPU launch arguments");
    const auto [entry, inserted] = functions_.try_emplace(kernel_name, nullptr);
    if (inserted) {
        const int result = aclrtBinaryGetFunction(binary_, kernel_name, &entry->second);
        if (result != ACL_SUCCESS || entry->second == nullptr) {
            functions_.erase(entry);
            return unexpected(ErrorCode::kRuntime, "AICPU kernel entry lookup failed: " + std::string(kernel_name));
        }
    }
    aclrtLaunchKernelAttr attribute{};
    attribute.id = ACL_RT_LAUNCH_KERNEL_ATTR_TIMEOUT;
    attribute.value.timeout = 5U;
    aclrtLaunchKernelCfg config{&attribute, 1U};
    const int launch_result = aclrtLaunchKernelWithHostArgs(entry->second, 1U, stream_, &config, &args_gm_addr,
                                                            sizeof(args_gm_addr), nullptr, 0U);
    if (launch_result != ACL_SUCCESS)
        return unexpected(ErrorCode::kRuntime, "AICPU kernel launch failed: " + std::string(kernel_name));
    const int sync_result = aclrtSynchronizeStreamWithTimeout(stream_, completion_timeout_ms);
    if (sync_result != ACL_SUCCESS)
        return unexpected(ErrorCode::kRuntime, "AICPU kernel synchronization failed: " + std::string(kernel_name));
    return {};
}

void AicpuLauncher::reset() noexcept {
    if (stream_ != nullptr)
        (void)aclrtDestroyStream(stream_);
    stream_ = nullptr;
    functions_.clear();
    if (binary_ != nullptr)
        (void)aclrtBinaryUnLoad(binary_);
    binary_ = nullptr;
}

bool AicpuLauncher::loaded() const noexcept {
    return binary_ != nullptr && stream_ != nullptr;
}

}  // namespace nds
