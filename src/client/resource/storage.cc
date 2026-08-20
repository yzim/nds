#include "storage.hh"

#include "aicpu/host/launcher.hh"
#include "aiv/host/launcher.hh"
#include "ra/ra.hh"

#include <cstddef>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace nds::client {
namespace {

constexpr std::uint32_t kCompletionTimeoutMs = 5000U;

class DeviceAllocation {
public:
    explicit DeviceAllocation(Runtime *runtime) : runtime_(runtime) {}
    DeviceAllocation(const DeviceAllocation &) = delete;
    DeviceAllocation &operator=(const DeviceAllocation &) = delete;

    Result<void> allocate(std::size_t size) {
        if (runtime_ == nullptr || buffer_.data() != nullptr)
            return unexpected(ErrorCode::kInvalidArgument, "device allocation requires one runtime and empty storage");
        auto allocated = runtime_->allocate(size);
        if (!allocated)
            return unexpected(allocated.error());
        buffer_ = std::move(*allocated);
        return {};
    }

    void *get() const noexcept {
        return buffer_.data();
    }

private:
    Runtime *runtime_{};
    MemoryBuffer buffer_;
};

Result<void> launch_aicpu(AicpuEntrypointLauncher *launcher, NdsDeviceStorageReadArgs *args) {
    return launcher->launch_storage_read_and_wait(args, kCompletionTimeoutMs);
}

Result<void> launch_aicpu(AicpuEntrypointLauncher *launcher, NdsDeviceStorageWriteArgs *args) {
    return launcher->launch_storage_write_and_wait(args, kCompletionTimeoutMs);
}

Result<void> launch_aicpu(AicpuEntrypointLauncher *launcher, NdsDeviceStorageBatchReadArgs *args) {
    return launcher->launch_storage_batch_read_and_wait(args, kCompletionTimeoutMs);
}

Result<void> launch_aicpu(AicpuEntrypointLauncher *launcher, NdsDeviceStorageBatchWriteArgs *args) {
    return launcher->launch_storage_batch_write_and_wait(args, kCompletionTimeoutMs);
}

template <typename Args, typename Command>
Result<void> submit_device_storage(Runtime *runtime, Transport *transport, const MemoryRegion &command_region,
                                   const MemoryRegion &completion, std::uint64_t capacity,
                                   StorageOperation operation, const Command &command) {
    const auto device_transport = transport->qp()->make_device_transport();
    if (!device_transport)
        return unexpected(device_transport.error());
    Args args{};
    args.abi_version = NDS_DEVICE_STORAGE_ABI_VERSION;
    args.size = sizeof(args);
    args.context.abi_version = NDS_DEVICE_STORAGE_ABI_VERSION;
    args.context.size = sizeof(args.context);
    args.context.transport = *device_transport;
    args.context.command_buffer = {command_region.address(), static_cast<std::uint32_t>(command_region.length()),
                                   command_region.local_key()};
    args.context.completion = {completion.address(), static_cast<std::uint32_t>(completion.length()),
                               completion.local_key()};
    args.context.capacity = capacity;
    args.command = command;

    DeviceAllocation result(runtime);
    if (const auto allocated = result.allocate(sizeof(NdsDeviceOperationResult)); !allocated)
        return unexpected(allocated.error());
    const NdsDeviceOperationResult pending{NDS_DEVICE_OPERATION_INVALID_ARGUMENT, NDS_DEVICE_OPERATION_PATH_NONE, 0,
                                           0U};
    if (const auto copied = runtime->copy_host_to_device(result.get(), &pending, sizeof(pending)); !copied)
        return unexpected(copied.error());
    args.operation_result_address = reinterpret_cast<std::uint64_t>(result.get());

    if (transport->execution().mode == NpuExecutionMode::Aicpu) {
        AicpuEntrypointLauncher launcher;
        if (const auto loaded = launcher.load(&runtime->acl_api(), transport->execution().aicpu_kernel_config); !loaded)
            return unexpected(loaded.error());
        if (const auto launched = launch_aicpu(&launcher, &args); !launched)
            return unexpected(launched.error());
    } else {
        AivEntrypointLauncher launcher;
        if (const auto loaded = launcher.load(&runtime->acl_api(), transport->execution().aiv_kernel); !loaded)
            return unexpected(loaded.error());
        DeviceAllocation device_args(runtime);
        if (const auto allocated = device_args.allocate(sizeof(args)); !allocated)
            return unexpected(allocated.error());
        if (const auto copied = runtime->copy_host_to_device(device_args.get(), &args, sizeof(args)); !copied)
            return unexpected(copied.error());
        if (const auto launched = launcher.launch_storage_and_wait(
                reinterpret_cast<std::uint64_t>(device_args.get()), operation, kCompletionTimeoutMs);
            !launched)
            return unexpected(launched.error());
    }

    NdsDeviceOperationResult completed{};
    if (const auto copied = runtime->copy_device_to_host(&completed, result.get(), sizeof(completed)); !copied)
        return unexpected(copied.error());
    if (completed.status != NDS_DEVICE_OPERATION_SUCCESS)
        return unexpected(ErrorCode::kRuntime, "device storage operation failed");
    return {};
}

}  // namespace

Result<void> StorageClient::open(Runtime *runtime, Transport *transport) {
    if (opened_ || runtime == nullptr || transport == nullptr || !runtime->initialized())
        return unexpected(ErrorCode::kInvalidArgument, "storage client requires one open runtime and transport");
    runtime_ = runtime;
    transport_ = transport;
    auto command_buffer = runtime_->allocate(kStorageCommandBytes);
    if (!command_buffer)
        return unexpected(command_buffer.error());
    command_buffer_ = std::move(*command_buffer);
    auto completion_buffer = runtime_->allocate(kStorageCompletionBytes);
    if (!completion_buffer)
        return unexpected(completion_buffer.error());
    completion_buffer_ = std::move(*completion_buffer);
    auto command_region = transport_->endpoint()->reg_mr(command_buffer_, MemoryAccess::DirectNpu);
    if (!command_region)
        return unexpected(command_region.error());
    command_region_ = std::move(*command_region);
    auto completion_region = transport_->endpoint()->reg_mr(completion_buffer_, MemoryAccess::DirectNpu);
    if (!completion_region)
        return unexpected(completion_region.error());
    completion_region_ = std::move(*completion_region);
    const auto capacity = exchange_bootstrap();
    if (!capacity)
        return unexpected(capacity.error());
    capacity_ = *capacity;
    const nds::transport::QpInfo &local = transport_->local_qp_info();
    next_command_id_ = (static_cast<std::uint64_t>(local.qp_num) << 32U) | local.psn;
    if (next_command_id_ == 0U)
        next_command_id_ = 1U;
    opened_ = true;
    return {};
}

Result<void> StorageClient::read(std::uint64_t offset, MemoryBuffer *data, std::uint32_t length) {
    if (!opened_)
        return unexpected(ErrorCode::kInvalidArgument, "storage read requires an open client");
    if (const auto valid = validate_io({offset, data, length}); !valid)
        return unexpected(valid.error());
    auto region = transport_->endpoint()->reg_mr(*data, MemoryAccess::DirectNpu);
    if (!region)
        return unexpected(region.error());
    return execute_storage_read({allocate_command_id(), offset, length,
                                 {region->address(), region->length(), region->remote_key()}});
}

Result<void> StorageClient::write(std::uint64_t offset, MemoryBuffer *data, std::uint32_t length) {
    if (!opened_)
        return unexpected(ErrorCode::kInvalidArgument, "storage write requires an open client");
    if (const auto valid = validate_io({offset, data, length}); !valid)
        return unexpected(valid.error());
    auto region = transport_->endpoint()->reg_mr(*data, MemoryAccess::DirectNpu);
    if (!region)
        return unexpected(region.error());
    return execute_storage_write({allocate_command_id(), offset, length,
                                  {region->address(), region->length(), region->remote_key()}});
}

Result<void> StorageClient::read_batch(std::span<const StorageIo> requests) {
    if (!opened_ || requests.empty())
        return unexpected(ErrorCode::kInvalidArgument, "storage batch read requires at least one request");
    if (requests.size() > kStorageMaxBatchEntries)
        return unexpected(ErrorCode::kInvalidArgument, "storage batch read exceeds the descriptor limit");
    std::vector<MemoryRegion> regions;
    regions.reserve(requests.size());
    std::uint64_t total_length{};
    for (const StorageIo &request : requests) {
        if (const auto valid = validate_io(request); !valid)
            return unexpected(valid.error());
        if (request.length > std::numeric_limits<std::uint64_t>::max() - total_length)
            return unexpected(ErrorCode::kInvalidArgument, "storage batch read byte count overflows");
        total_length += request.length;
        auto region = transport_->endpoint()->reg_mr(*request.data, MemoryAccess::DirectNpu);
        if (!region)
            return unexpected(region.error());
        regions.push_back(std::move(*region));
    }
    std::vector<uint8_t> bytes(requests.size() * kStorageBatchEntryBytes);
    for (std::size_t index = 0U; index < requests.size(); ++index) {
        const StorageBatchReadEntry entry{requests[index].offset, requests[index].length,
                                          {regions[index].address(), regions[index].length(),
                                           regions[index].remote_key()}};
        if (serialize_storage_batch_read_entry(entry, bytes.data() + index * kStorageBatchEntryBytes,
                                               kStorageBatchEntryBytes) != StorageSerdeResult::Ok)
            return unexpected(ErrorCode::kProtocol, "invalid storage batch-read entry");
    }
    auto buffer = runtime_->allocate(bytes.size());
    if (!buffer)
        return unexpected(buffer.error());
    if (const auto copied = runtime_->copy_to(&*buffer, bytes.data(), bytes.size()); !copied)
        return unexpected(copied.error());
    auto region = transport_->endpoint()->reg_mr(*buffer, MemoryAccess::DirectNpu);
    if (!region)
        return unexpected(region.error());
    return execute_storage_batch_read({allocate_command_id(), requests.size(), total_length,
                                       {region->address(), region->length(), region->remote_key()}});
}

Result<void> StorageClient::write_batch(std::span<const StorageIo> requests) {
    if (!opened_ || requests.empty())
        return unexpected(ErrorCode::kInvalidArgument, "storage batch write requires at least one request");
    if (requests.size() > kStorageMaxBatchEntries)
        return unexpected(ErrorCode::kInvalidArgument, "storage batch write exceeds the descriptor limit");
    std::vector<MemoryRegion> regions;
    regions.reserve(requests.size());
    std::uint64_t total_length{};
    for (const StorageIo &request : requests) {
        if (const auto valid = validate_io(request); !valid)
            return unexpected(valid.error());
        if (request.length > std::numeric_limits<std::uint64_t>::max() - total_length)
            return unexpected(ErrorCode::kInvalidArgument, "storage batch write byte count overflows");
        total_length += request.length;
        auto region = transport_->endpoint()->reg_mr(*request.data, MemoryAccess::DirectNpu);
        if (!region)
            return unexpected(region.error());
        regions.push_back(std::move(*region));
    }
    std::vector<uint8_t> bytes(requests.size() * kStorageBatchEntryBytes);
    for (std::size_t index = 0U; index < requests.size(); ++index) {
        const StorageBatchWriteEntry entry{requests[index].offset, requests[index].length,
                                           {regions[index].address(), regions[index].length(),
                                            regions[index].remote_key()}};
        if (serialize_storage_batch_write_entry(entry, bytes.data() + index * kStorageBatchEntryBytes,
                                                kStorageBatchEntryBytes) != StorageSerdeResult::Ok)
            return unexpected(ErrorCode::kProtocol, "invalid storage batch-write entry");
    }
    auto buffer = runtime_->allocate(bytes.size());
    if (!buffer)
        return unexpected(buffer.error());
    if (const auto copied = runtime_->copy_to(&*buffer, bytes.data(), bytes.size()); !copied)
        return unexpected(copied.error());
    auto region = transport_->endpoint()->reg_mr(*buffer, MemoryAccess::DirectNpu);
    if (!region)
        return unexpected(region.error());
    return execute_storage_batch_write({allocate_command_id(), requests.size(), total_length,
                                        {region->address(), region->length(), region->remote_key()}});
}

std::uint64_t StorageClient::capacity() const noexcept {
    return capacity_;
}

Result<void> StorageClient::validate_io(const StorageIo &request) const {
    if (request.data == nullptr || request.length == 0U || request.length > request.data->size())
        return unexpected(ErrorCode::kInvalidArgument, "storage operation requires a sufficiently large data buffer");
    if (request.offset > capacity_ || request.length > capacity_ - request.offset)
        return unexpected(ErrorCode::kProtocol, "requested storage range exceeds namespace capacity");
    return {};
}

std::uint64_t StorageClient::allocate_command_id() noexcept {
    const std::uint64_t command_id = next_command_id_++;
    if (next_command_id_ == 0U)
        ++next_command_id_;
    return command_id;
}

Result<void> StorageClient::execute_storage_read(const StorageReadCommand &command) {
    if (const auto ready = transport_->ready(); !ready)
        return unexpected(ready.error());
    if (transport_->execution().mode == NpuExecutionMode::Ra) {
        const RaStorageContext context{{runtime_, transport_->qp()},
                                       command_buffer_.data(),
                                       {command_region_.address(), static_cast<std::uint32_t>(command_region_.length()),
                                        command_region_.local_key()},
                                       completion_buffer_.data(),
                                       {completion_region_.address(),
                                        static_cast<std::uint32_t>(completion_region_.length()),
                                        completion_region_.local_key()},
                                       capacity_};
        return NdsRaStorageRead(context, command);
    }
    return submit_device_storage<NdsDeviceStorageReadArgs>(runtime_, transport_, command_region_, completion_region_,
                                                           capacity_, StorageOperation::Read, command);
}

Result<void> StorageClient::execute_storage_write(const StorageWriteCommand &command) {
    if (const auto ready = transport_->ready(); !ready)
        return unexpected(ready.error());
    if (transport_->execution().mode == NpuExecutionMode::Ra) {
        const RaStorageContext context{{runtime_, transport_->qp()}, command_buffer_.data(),
                                       {command_region_.address(), static_cast<std::uint32_t>(command_region_.length()),
                                        command_region_.local_key()},
                                       completion_buffer_.data(),
                                       {completion_region_.address(),
                                        static_cast<std::uint32_t>(completion_region_.length()),
                                        completion_region_.local_key()},
                                       capacity_};
        return NdsRaStorageWrite(context, command);
    }
    return submit_device_storage<NdsDeviceStorageWriteArgs>(runtime_, transport_, command_region_, completion_region_,
                                                            capacity_, StorageOperation::Write, command);
}

Result<void> StorageClient::execute_storage_batch_read(const StorageBatchReadCommand &command) {
    if (const auto ready = transport_->ready(); !ready)
        return unexpected(ready.error());
    if (transport_->execution().mode == NpuExecutionMode::Ra) {
        const RaStorageContext context{{runtime_, transport_->qp()}, command_buffer_.data(),
                                       {command_region_.address(), static_cast<std::uint32_t>(command_region_.length()),
                                        command_region_.local_key()},
                                       completion_buffer_.data(),
                                       {completion_region_.address(),
                                        static_cast<std::uint32_t>(completion_region_.length()),
                                        completion_region_.local_key()},
                                       capacity_};
        return NdsRaStorageBatchRead(context, command);
    }
    return submit_device_storage<NdsDeviceStorageBatchReadArgs>(
        runtime_, transport_, command_region_, completion_region_, capacity_, StorageOperation::BatchRead, command);
}

Result<void> StorageClient::execute_storage_batch_write(const StorageBatchWriteCommand &command) {
    if (const auto ready = transport_->ready(); !ready)
        return unexpected(ready.error());
    if (transport_->execution().mode == NpuExecutionMode::Ra) {
        const RaStorageContext context{{runtime_, transport_->qp()}, command_buffer_.data(),
                                       {command_region_.address(), static_cast<std::uint32_t>(command_region_.length()),
                                        command_region_.local_key()},
                                       completion_buffer_.data(),
                                       {completion_region_.address(),
                                        static_cast<std::uint32_t>(completion_region_.length()),
                                        completion_region_.local_key()},
                                       capacity_};
        return NdsRaStorageBatchWrite(context, command);
    }
    return submit_device_storage<NdsDeviceStorageBatchWriteArgs>(
        runtime_, transport_, command_region_, completion_region_, capacity_, StorageOperation::BatchWrite, command);
}

Result<std::uint64_t> StorageClient::exchange_bootstrap() {
    const StorageBootstrap bootstrap{{completion_region_.address(), completion_region_.length(),
                                      completion_region_.remote_key()}};
    uint8_t bootstrap_bytes[kStorageBootstrapBytes]{};
    uint8_t namespace_bytes[kStorageNamespaceBytes]{};
    StorageNamespace storage_namespace{};
    if (serialize_storage_bootstrap(bootstrap, bootstrap_bytes, sizeof(bootstrap_bytes)) != StorageSerdeResult::Ok)
        return unexpected(ErrorCode::kProtocol, "invalid storage bootstrap record");
    if (const auto sent = transport_->bootstrap()->send_bytes(bootstrap_bytes, sizeof(bootstrap_bytes)); !sent)
        return unexpected(sent.error());
    if (const auto received = transport_->bootstrap()->receive_bytes(namespace_bytes, sizeof(namespace_bytes)); !received)
        return unexpected(received.error());
    if (deserialize_storage_namespace(namespace_bytes, sizeof(namespace_bytes), &storage_namespace) !=
        StorageSerdeResult::Ok)
        return unexpected(ErrorCode::kProtocol, "invalid storage namespace record");
    return storage_namespace.capacity;
}

}  // namespace nds::client
