#include "storage.hh"

#include "backends/aicpu/launcher.hh"
#include "backends/aiv/launcher.hh"
#include "ra/ra.hh"

#include <chrono>
#include <cstddef>
#include <limits>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace nds::client {
namespace {

constexpr std::uint32_t kCompletionTimeoutMs = 5000U;

struct DeviceStorageWaitResult {
    std::int32_t return_value;
    bool terminal;
};

struct AivThreeAddressArguments {
    std::uint64_t first_address;
    std::uint64_t second_address;
    std::uint64_t return_value_address;
};

struct AivStorageWaitArguments {
    std::uint64_t context_address;
    std::uint64_t command_id;
    std::uint64_t expected_bytes;
    std::uint64_t return_value_address;
};

class DeviceAllocation {
public:
    explicit DeviceAllocation(Runtime *runtime) : runtime_(runtime) {}
    DeviceAllocation(const DeviceAllocation &) = delete;
    DeviceAllocation &operator=(const DeviceAllocation &) = delete;

    Result<void> allocate(std::size_t size) {
        if (runtime_ == nullptr || buffer_.data() != nullptr)
            return Error{ErrorCode::kInvalidArgument, "device allocation requires one runtime and empty storage"};
        auto allocated = runtime_->allocate(size);
        if (!allocated)
            return Error{allocated.error()};
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

Result<void> launch_aicpu(AicpuLauncher *launcher, std::uint64_t args_address, NdsDeviceStorageReadArgs *) {
    return launcher->launch_and_wait("nds_aicpu_storage_read_kernel", args_address, kCompletionTimeoutMs);
}

Result<void> launch_aicpu(AicpuLauncher *launcher, std::uint64_t args_address, NdsDeviceStorageWriteArgs *) {
    return launcher->launch_and_wait("nds_aicpu_storage_write_kernel", args_address, kCompletionTimeoutMs);
}

Result<void> launch_aicpu(AicpuLauncher *launcher, std::uint64_t args_address, NdsDeviceStorageBatchReadArgs *) {
    return launcher->launch_and_wait("nds_aicpu_storage_batch_read_kernel", args_address, kCompletionTimeoutMs);
}

Result<void> launch_aicpu(AicpuLauncher *launcher, std::uint64_t args_address, NdsDeviceStorageBatchWriteArgs *) {
    return launcher->launch_and_wait("nds_aicpu_storage_batch_write_kernel", args_address, kCompletionTimeoutMs);
}

const char *aiv_storage_kernel(StorageOperation operation) {
    switch (operation) {
        case StorageOperation::Read:
            return "nds_aiv_storage_read_kernel";
        case StorageOperation::Write:
            return "nds_aiv_storage_write_kernel";
        case StorageOperation::BatchRead:
            return "nds_aiv_storage_batch_read_kernel";
        case StorageOperation::BatchWrite:
            return "nds_aiv_storage_batch_write_kernel";
    }
    return nullptr;
}

template <typename Args, typename Command>
Result<void> submit_device_storage(Runtime *runtime, Transport *transport, const BackendConfig &backend,
                                   const MemoryRegion &command_region, const MemoryRegion &completion,
                                   std::uint64_t capacity, StorageOperation operation, const Command &command) {
    if (transport == nullptr)
        return Error{ErrorCode::kInvalidArgument, "storage requires a transport"};
    NdsDeviceStorageContext context{};
    context.transport = transport->device_transport();
    context.command_buffer = {command_region.address(), static_cast<std::uint32_t>(command_region.length()),
                              command_region.local_key()};
    context.completion = {completion.address(), static_cast<std::uint32_t>(completion.length()),
                          completion.local_key()};
    context.capacity = capacity;

    if (backend.mode == NpuBackend::Aicpu) {
        Args args{};
        args.context = context;
        args.command = command;
        args.return_value = std::numeric_limits<std::int32_t>::min();
        DeviceAllocation device_args(runtime);
        if (const auto allocated = device_args.allocate(sizeof(args)); !allocated)
            return Error{allocated.error()};
        if (const auto copied = runtime->copy_host_to_device(device_args.get(), &args, sizeof(args)); !copied)
            return Error{copied.error()};
        AicpuLauncher launcher;
        if (const auto loaded = launcher.load(backend.aicpu_kernel); !loaded)
            return Error{loaded.error()};
        if (const auto launched = launch_aicpu(&launcher, reinterpret_cast<std::uint64_t>(device_args.get()), &args);
            !launched)
            return Error{launched.error()};
        if (const auto copied = runtime->copy_device_to_host(&args, device_args.get(), sizeof(args)); !copied)
            return Error{copied.error()};
        if (args.return_value != 0)
            return Error{ErrorCode::kRuntime, "device storage operation failed: " + std::to_string(args.return_value)};
    } else {
        std::int32_t return_value = std::numeric_limits<std::int32_t>::min();
        DeviceAllocation device_context(runtime);
        DeviceAllocation device_command(runtime);
        DeviceAllocation device_return_value(runtime);
        if (const auto allocated = device_context.allocate(sizeof(context)); !allocated)
            return Error{allocated.error()};
        if (const auto allocated = device_command.allocate(sizeof(command)); !allocated)
            return Error{allocated.error()};
        if (const auto allocated = device_return_value.allocate(sizeof(return_value)); !allocated)
            return Error{allocated.error()};
        if (const auto copied = runtime->copy_host_to_device(device_context.get(), &context, sizeof(context)); !copied)
            return Error{copied.error()};
        if (const auto copied = runtime->copy_host_to_device(device_command.get(), &command, sizeof(command)); !copied)
            return Error{copied.error()};
        if (const auto copied =
                runtime->copy_host_to_device(device_return_value.get(), &return_value, sizeof(return_value));
            !copied)
            return Error{copied.error()};
        AivLauncher launcher;
        if (const auto loaded = launcher.load(backend.aiv_kernel); !loaded)
            return Error{loaded.error()};
        const char *kernel_name = aiv_storage_kernel(operation);
        if (kernel_name == nullptr)
            return Error{ErrorCode::kInvalidArgument, "device storage operation is invalid"};
        AivThreeAddressArguments arguments{reinterpret_cast<std::uint64_t>(device_context.get()),
                                           reinterpret_cast<std::uint64_t>(device_command.get()),
                                           reinterpret_cast<std::uint64_t>(device_return_value.get())};
        if (const auto launched =
                launcher.launch_and_wait(kernel_name, &arguments, sizeof(arguments), kCompletionTimeoutMs);
            !launched)
            return Error{launched.error()};
        if (const auto copied =
                runtime->copy_device_to_host(&return_value, device_return_value.get(), sizeof(return_value));
            !copied)
            return Error{copied.error()};
        if (return_value != 0)
            return Error{ErrorCode::kRuntime, "device storage operation failed: " + std::to_string(return_value)};
    }
    return {};
}

Result<DeviceStorageWaitResult> wait_device_storage(Runtime *runtime, Transport *transport,
                                                    const BackendConfig &backend, const MemoryRegion &command_region,
                                                    const MemoryRegion &completion, std::uint64_t capacity,
                                                    std::uint64_t command_id, std::uint64_t expected_bytes,
                                                    std::int32_t timeout_ms) {
    if (transport == nullptr)
        return Error{ErrorCode::kInvalidArgument, "storage requires a transport"};
    NdsDeviceStorageContext context{};
    context.transport = transport->device_transport();
    context.command_buffer = {command_region.address(), static_cast<std::uint32_t>(command_region.length()),
                              command_region.local_key()};
    context.completion = {completion.address(), static_cast<std::uint32_t>(completion.length()),
                          completion.local_key()};
    context.capacity = capacity;
    std::int32_t return_value = std::numeric_limits<std::int32_t>::min();

    if (backend.mode == NpuBackend::Aicpu) {
        NdsDeviceStorageWaitArgs args{};
        args.context = context;
        args.command_id = command_id;
        args.expected_bytes = expected_bytes;
        args.return_value = return_value;
        DeviceAllocation device_args(runtime);
        if (const auto allocated = device_args.allocate(sizeof(args)); !allocated)
            return Error{allocated.error()};
        if (const auto copied = runtime->copy_host_to_device(device_args.get(), &args, sizeof(args)); !copied)
            return Error{copied.error()};
        AicpuLauncher launcher;
        if (const auto loaded = launcher.load(backend.aicpu_kernel); !loaded)
            return Error{loaded.error()};
        if (const auto launched = launcher.launch_and_wait(
                "nds_aicpu_storage_wait_kernel", reinterpret_cast<std::uint64_t>(device_args.get()), timeout_ms);
            !launched)
            return Error{launched.error()};
        if (const auto copied = runtime->copy_device_to_host(&args, device_args.get(), sizeof(args)); !copied)
            return Error{copied.error()};
        return_value = args.return_value;
    } else {
        DeviceAllocation device_context(runtime);
        DeviceAllocation device_return_value(runtime);
        if (const auto allocated = device_context.allocate(sizeof(context)); !allocated)
            return Error{allocated.error()};
        if (const auto allocated = device_return_value.allocate(sizeof(return_value)); !allocated)
            return Error{allocated.error()};
        if (const auto copied = runtime->copy_host_to_device(device_context.get(), &context, sizeof(context)); !copied)
            return Error{copied.error()};
        if (const auto copied =
                runtime->copy_host_to_device(device_return_value.get(), &return_value, sizeof(return_value));
            !copied)
            return Error{copied.error()};
        AivLauncher launcher;
        if (const auto loaded = launcher.load(backend.aiv_kernel); !loaded)
            return Error{loaded.error()};
        AivStorageWaitArguments arguments{reinterpret_cast<std::uint64_t>(device_context.get()), command_id,
                                          expected_bytes, reinterpret_cast<std::uint64_t>(device_return_value.get())};
        if (const auto launched =
                launcher.launch_and_wait("nds_aiv_storage_wait_kernel", &arguments, sizeof(arguments), timeout_ms);
            !launched)
            return Error{launched.error()};
        if (const auto copied =
                runtime->copy_device_to_host(&return_value, device_return_value.get(), sizeof(return_value));
            !copied)
            return Error{copied.error()};
    }
    std::uint8_t completion_bytes[kStorageCompletionBytes]{};
    if (const auto copied = runtime->copy_device_to_host(
            completion_bytes, reinterpret_cast<void *>(completion.address()), sizeof(completion_bytes));
        !copied)
        return Error{copied.error()};
    StorageCompletion observed{};
    const bool terminal = deserialize_storage_completion(completion_bytes, sizeof(completion_bytes), &observed) ==
                              StorageSerdeResult::Ok &&
                          observed.state == StorageCompletionState::Complete && observed.command_id == command_id &&
                          observed.bytes_transferred == expected_bytes;
    return DeviceStorageWaitResult{return_value, terminal};
}

}  // namespace

Result<void> StorageClient::open(Runtime *runtime, Transport *transport) {
    if (opened_ || runtime == nullptr || transport == nullptr || !runtime->initialized())
        return Error{ErrorCode::kInvalidArgument, "storage client requires one open runtime and transport"};
    runtime_ = runtime;
    transport_ = transport;
    auto command_buffer = runtime_->allocate(kStorageCommandBytes);
    if (!command_buffer)
        return Error{command_buffer.error()};
    command_buffer_ = std::move(*command_buffer);
    auto completion_buffer = runtime_->allocate(kStorageCompletionBytes);
    if (!completion_buffer)
        return Error{completion_buffer.error()};
    completion_buffer_ = std::move(*completion_buffer);
    auto namespace_buffer = runtime_->allocate(kStorageNamespaceBytes);
    if (!namespace_buffer)
        return Error{namespace_buffer.error()};
    namespace_buffer_ = std::move(*namespace_buffer);
    auto command_region = transport_->register_memory(command_buffer_, MemoryAccess::DirectNpu);
    if (!command_region)
        return Error{command_region.error()};
    command_region_ = std::move(*command_region);
    auto completion_region = transport_->register_memory(completion_buffer_, MemoryAccess::DirectNpu);
    if (!completion_region)
        return Error{completion_region.error()};
    completion_region_ = std::move(*completion_region);
    auto namespace_region = transport_->register_memory(namespace_buffer_, MemoryAccess::DirectNpu);
    if (!namespace_region)
        return Error{namespace_region.error()};
    namespace_region_ = std::move(*namespace_region);
    const auto capacity = exchange_bootstrap();
    if (!capacity)
        return Error{capacity.error()};
    capacity_ = *capacity;
    const nds::transport::QpInfo &local = transport_->local_qp_info();
    next_command_id_ = (static_cast<std::uint64_t>(local.qp_num) << 32U) | local.psn;
    if (next_command_id_ == 0U)
        next_command_id_ = 1U;
    opened_ = true;
    return {};
}

Result<StorageCompletionHandle> StorageClient::read(std::uint64_t offset, MemoryBuffer *data, std::uint32_t length) {
    if (const auto ready = begin_submission(); !ready)
        return Error{ready.error()};
    if (const auto valid = validate_io({offset, data, length}); !valid)
        return Error{valid.error()};
    auto region = transport_->register_memory(*data, MemoryAccess::DirectNpu);
    if (!region)
        return Error{region.error()};
    const std::uint64_t command_id = allocate_command_id();
    PendingRequest pending{};
    pending.command_id = command_id;
    pending.expected_bytes = length;
    const StorageReadCommand command{
        command_id, offset, length, {region->address(), region->length(), region->remote_key()}};
    pending.data_regions.push_back(std::move(*region));
    if (const auto submitted = execute_storage_read(command); !submitted)
        return Error{submitted.error()};
    pending_ = std::move(pending);
    return StorageCompletionHandle{command_id};
}

Result<StorageCompletionHandle> StorageClient::write(std::uint64_t offset, MemoryBuffer *data, std::uint32_t length) {
    if (const auto ready = begin_submission(); !ready)
        return Error{ready.error()};
    if (const auto valid = validate_io({offset, data, length}); !valid)
        return Error{valid.error()};
    auto region = transport_->register_memory(*data, MemoryAccess::DirectNpu);
    if (!region)
        return Error{region.error()};
    const std::uint64_t command_id = allocate_command_id();
    PendingRequest pending{};
    pending.command_id = command_id;
    pending.expected_bytes = length;
    const StorageWriteCommand command{
        command_id, offset, length, {region->address(), region->length(), region->remote_key()}};
    pending.data_regions.push_back(std::move(*region));
    if (const auto submitted = execute_storage_write(command); !submitted)
        return Error{submitted.error()};
    pending_ = std::move(pending);
    return StorageCompletionHandle{command_id};
}

Result<StorageCompletionHandle> StorageClient::read_batch(std::span<const StorageIo> requests) {
    if (const auto ready = begin_submission(); !ready)
        return Error{ready.error()};
    if (requests.empty())
        return Error{ErrorCode::kInvalidArgument, "storage batch read requires at least one request"};
    if (requests.size() > kStorageMaxBatchEntries)
        return Error{ErrorCode::kInvalidArgument, "storage batch read exceeds the descriptor limit"};
    const std::uint64_t command_id = allocate_command_id();
    PendingRequest pending{};
    pending.command_id = command_id;
    pending.data_regions.reserve(requests.size());
    std::uint64_t total_length{};
    for (const StorageIo &request : requests) {
        if (const auto valid = validate_io(request); !valid)
            return Error{valid.error()};
        if (request.length > std::numeric_limits<std::uint64_t>::max() - total_length)
            return Error{ErrorCode::kInvalidArgument, "storage batch read byte count overflows"};
        total_length += request.length;
        auto region = transport_->register_memory(*request.data, MemoryAccess::DirectNpu);
        if (!region)
            return Error{region.error()};
        pending.data_regions.push_back(std::move(*region));
    }
    std::vector<uint8_t> bytes(requests.size() * kStorageBatchEntryBytes);
    for (std::size_t index = 0U; index < requests.size(); ++index) {
        const StorageBatchReadEntry entry{requests[index].offset,
                                          requests[index].length,
                                          {pending.data_regions[index].address(), pending.data_regions[index].length(),
                                           pending.data_regions[index].remote_key()}};
        if (serialize_storage_batch_read_entry(entry, bytes.data() + index * kStorageBatchEntryBytes,
                                               kStorageBatchEntryBytes) != StorageSerdeResult::Ok)
            return Error{ErrorCode::kProtocol, "invalid storage batch-read entry"};
    }
    auto descriptor_buffer = runtime_->allocate(bytes.size());
    if (!descriptor_buffer)
        return Error{descriptor_buffer.error()};
    if (const auto copied = runtime_->copy_to(&*descriptor_buffer, bytes.data(), bytes.size()); !copied)
        return Error{copied.error()};
    auto region = transport_->register_memory(*descriptor_buffer, MemoryAccess::DirectNpu);
    if (!region)
        return Error{region.error()};
    pending.expected_bytes = total_length;
    const StorageBatchReadCommand command{
        command_id, requests.size(), total_length, {region->address(), region->length(), region->remote_key()}};
    pending.descriptor_buffer = std::move(*descriptor_buffer);
    pending.descriptor_region = std::move(*region);
    if (const auto submitted = execute_storage_batch_read(command); !submitted)
        return Error{submitted.error()};
    pending_ = std::move(pending);
    return StorageCompletionHandle{command_id};
}

Result<StorageCompletionHandle> StorageClient::write_batch(std::span<const StorageIo> requests) {
    if (const auto ready = begin_submission(); !ready)
        return Error{ready.error()};
    if (requests.empty())
        return Error{ErrorCode::kInvalidArgument, "storage batch write requires at least one request"};
    if (requests.size() > kStorageMaxBatchEntries)
        return Error{ErrorCode::kInvalidArgument, "storage batch write exceeds the descriptor limit"};
    const std::uint64_t command_id = allocate_command_id();
    PendingRequest pending{};
    pending.command_id = command_id;
    pending.data_regions.reserve(requests.size());
    std::uint64_t total_length{};
    for (const StorageIo &request : requests) {
        if (const auto valid = validate_io(request); !valid)
            return Error{valid.error()};
        if (request.length > std::numeric_limits<std::uint64_t>::max() - total_length)
            return Error{ErrorCode::kInvalidArgument, "storage batch write byte count overflows"};
        total_length += request.length;
        auto region = transport_->register_memory(*request.data, MemoryAccess::DirectNpu);
        if (!region)
            return Error{region.error()};
        pending.data_regions.push_back(std::move(*region));
    }
    std::vector<uint8_t> bytes(requests.size() * kStorageBatchEntryBytes);
    for (std::size_t index = 0U; index < requests.size(); ++index) {
        const StorageBatchWriteEntry entry{requests[index].offset,
                                           requests[index].length,
                                           {pending.data_regions[index].address(), pending.data_regions[index].length(),
                                            pending.data_regions[index].remote_key()}};
        if (serialize_storage_batch_write_entry(entry, bytes.data() + index * kStorageBatchEntryBytes,
                                                kStorageBatchEntryBytes) != StorageSerdeResult::Ok)
            return Error{ErrorCode::kProtocol, "invalid storage batch-write entry"};
    }
    auto descriptor_buffer = runtime_->allocate(bytes.size());
    if (!descriptor_buffer)
        return Error{descriptor_buffer.error()};
    if (const auto copied = runtime_->copy_to(&*descriptor_buffer, bytes.data(), bytes.size()); !copied)
        return Error{copied.error()};
    auto region = transport_->register_memory(*descriptor_buffer, MemoryAccess::DirectNpu);
    if (!region)
        return Error{region.error()};
    pending.expected_bytes = total_length;
    const StorageBatchWriteCommand command{
        command_id, requests.size(), total_length, {region->address(), region->length(), region->remote_key()}};
    pending.descriptor_buffer = std::move(*descriptor_buffer);
    pending.descriptor_region = std::move(*region);
    if (const auto submitted = execute_storage_batch_write(command); !submitted)
        return Error{submitted.error()};
    pending_ = std::move(pending);
    return StorageCompletionHandle{command_id};
}

std::uint64_t StorageClient::capacity() const noexcept {
    return capacity_;
}

Result<void> StorageClient::validate_io(const StorageIo &request) const {
    if (request.data == nullptr || request.length == 0U || request.length > request.data->size())
        return Error{ErrorCode::kInvalidArgument, "storage operation requires a sufficiently large data buffer"};
    if (request.offset > capacity_ || request.length > capacity_ - request.offset)
        return Error{ErrorCode::kProtocol, "requested storage range exceeds namespace capacity"};
    return {};
}

Result<void> StorageClient::begin_submission() {
    if (!opened_)
        return Error{ErrorCode::kInvalidArgument, "storage submission requires an open client"};
    if (pending_.has_value())
        return Error{ErrorCode::kInvalidArgument, "storage client already has one command in flight"};
    return {};
}

Result<void> StorageClient::wait(StorageCompletionHandle handle, std::uint32_t timeout_ms) {
    if (!opened_ || !pending_.has_value() || handle.command_id == 0U || handle.command_id != pending_->command_id)
        return Error{ErrorCode::kInvalidArgument, "storage wait requires the current completion handle"};
    if (timeout_ms == 0U || timeout_ms > static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max()))
        return Error{ErrorCode::kInvalidArgument, "storage wait requires a positive timeout"};
    if (transport_->backend().mode == NpuBackend::Ra) {
        const auto completion = observe_completion(handle.command_id, pending_->expected_bytes, timeout_ms);
        if (!completion)
            return Error{completion.error()};
        pending_.reset();
        if (completion->status != StorageStatus::Success)
            return Error{ErrorCode::kProtocol, "storage completion returned a failure status"};
        return {};
    }
    const auto completed =
        wait_device_storage(runtime_, transport_, transport_->backend(), command_region_, completion_region_, capacity_,
                            handle.command_id, pending_->expected_bytes, static_cast<std::int32_t>(timeout_ms));
    if (!completed)
        return Error{completed.error()};
    if (completed->terminal)
        pending_.reset();
    if (completed->return_value != 0)
        return Error{ErrorCode::kProtocol, "device storage wait returned a failure status"};
    return {};
}

Result<StorageCompletion> StorageClient::observe_completion(std::uint64_t command_id, std::uint64_t expected_bytes,
                                                            std::uint32_t timeout_ms) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (std::chrono::steady_clock::now() < deadline) {
        uint8_t bytes[kStorageCompletionBytes]{};
        if (const auto copied = runtime_->copy_device_to_host(bytes, completion_buffer_.data(), sizeof(bytes)); !copied)
            return Error{copied.error()};
        StorageCompletion completion{};
        if (deserialize_storage_completion(bytes, sizeof(bytes), &completion) != StorageSerdeResult::Ok)
            return Error{ErrorCode::kProtocol, "invalid storage completion record"};
        if (completion.state != StorageCompletionState::Complete) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }
        if (completion.command_id != command_id || completion.bytes_transferred != expected_bytes)
            return Error{ErrorCode::kProtocol, "storage completion does not match the submitted command"};
        return completion;
    }
    return Error{ErrorCode::kProtocol, "timed out waiting for storage completion"};
}

std::uint64_t StorageClient::allocate_command_id() noexcept {
    const std::uint64_t command_id = next_command_id_++;
    if (next_command_id_ == 0U)
        ++next_command_id_;
    return command_id;
}

Result<void> StorageClient::execute_storage_read(const StorageReadCommand &command) {
    if (const auto ready = transport_->ready(); !ready)
        return Error{ready.error()};
    if (transport_->backend().mode == NpuBackend::Ra) {
        const RaStorageContext context{
            {runtime_, transport_->qp()},
            command_buffer_.data(),
            {command_region_.address(), static_cast<std::uint32_t>(command_region_.length()),
             command_region_.local_key()},
            completion_buffer_.data(),
            {completion_region_.address(), static_cast<std::uint32_t>(completion_region_.length()),
             completion_region_.local_key()},
            capacity_};
        return NdsRaStorageRead(context, command);
    }
    return submit_device_storage<NdsDeviceStorageReadArgs>(runtime_, transport_, transport_->backend(), command_region_,
                                                           completion_region_, capacity_, StorageOperation::Read,
                                                           command);
}

Result<void> StorageClient::execute_storage_write(const StorageWriteCommand &command) {
    if (const auto ready = transport_->ready(); !ready)
        return Error{ready.error()};
    if (transport_->backend().mode == NpuBackend::Ra) {
        const RaStorageContext context{
            {runtime_, transport_->qp()},
            command_buffer_.data(),
            {command_region_.address(), static_cast<std::uint32_t>(command_region_.length()),
             command_region_.local_key()},
            completion_buffer_.data(),
            {completion_region_.address(), static_cast<std::uint32_t>(completion_region_.length()),
             completion_region_.local_key()},
            capacity_};
        return NdsRaStorageWrite(context, command);
    }
    return submit_device_storage<NdsDeviceStorageWriteArgs>(runtime_, transport_, transport_->backend(),
                                                            command_region_, completion_region_, capacity_,
                                                            StorageOperation::Write, command);
}

Result<void> StorageClient::execute_storage_batch_read(const StorageBatchReadCommand &command) {
    if (const auto ready = transport_->ready(); !ready)
        return Error{ready.error()};
    if (transport_->backend().mode == NpuBackend::Ra) {
        const RaStorageContext context{
            {runtime_, transport_->qp()},
            command_buffer_.data(),
            {command_region_.address(), static_cast<std::uint32_t>(command_region_.length()),
             command_region_.local_key()},
            completion_buffer_.data(),
            {completion_region_.address(), static_cast<std::uint32_t>(completion_region_.length()),
             completion_region_.local_key()},
            capacity_};
        return NdsRaStorageBatchRead(context, command);
    }
    return submit_device_storage<NdsDeviceStorageBatchReadArgs>(runtime_, transport_, transport_->backend(),
                                                                command_region_, completion_region_, capacity_,
                                                                StorageOperation::BatchRead, command);
}

Result<void> StorageClient::execute_storage_batch_write(const StorageBatchWriteCommand &command) {
    if (const auto ready = transport_->ready(); !ready)
        return Error{ready.error()};
    if (transport_->backend().mode == NpuBackend::Ra) {
        const RaStorageContext context{
            {runtime_, transport_->qp()},
            command_buffer_.data(),
            {command_region_.address(), static_cast<std::uint32_t>(command_region_.length()),
             command_region_.local_key()},
            completion_buffer_.data(),
            {completion_region_.address(), static_cast<std::uint32_t>(completion_region_.length()),
             completion_region_.local_key()},
            capacity_};
        return NdsRaStorageBatchWrite(context, command);
    }
    return submit_device_storage<NdsDeviceStorageBatchWriteArgs>(runtime_, transport_, transport_->backend(),
                                                                 command_region_, completion_region_, capacity_,
                                                                 StorageOperation::BatchWrite, command);
}

Result<std::uint64_t> StorageClient::exchange_bootstrap() {
    const StorageBootstrap bootstrap{
        {completion_region_.address(), completion_region_.length(), completion_region_.remote_key()},
        {namespace_region_.address(), namespace_region_.length(), namespace_region_.remote_key()}};
    uint8_t bootstrap_bytes[kStorageBootstrapBytes]{};
    uint8_t namespace_bytes[kStorageNamespaceBytes]{};
    StorageNamespace storage_namespace{};
    if (serialize_storage_bootstrap(bootstrap, bootstrap_bytes, sizeof(bootstrap_bytes)) != StorageSerdeResult::Ok)
        return Error{ErrorCode::kProtocol, "invalid storage bootstrap record"};
    if (const auto cleared = runtime_->copy_to(&namespace_buffer_, namespace_bytes, sizeof(namespace_bytes)); !cleared)
        return Error{cleared.error()};
    if (const auto copied = runtime_->copy_to(&command_buffer_, bootstrap_bytes, sizeof(bootstrap_bytes)); !copied)
        return Error{copied.error()};
    if (const auto ready = transport_->ready(); !ready)
        return Error{ready.error()};
    const auto queue = transport_->queue(0U);
    if (!queue)
        return Error{queue.error()};
    if (const auto sent = transport_->send(*queue, {&command_region_, kStorageBootstrapBytes}); !sent)
        return Error{sent.error()};

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(kCompletionTimeoutMs);
    while (std::chrono::steady_clock::now() < deadline) {
        if (const auto copied = runtime_->copy_from(namespace_bytes, namespace_buffer_, sizeof(namespace_bytes));
            !copied)
            return Error{copied.error()};
        if (deserialize_storage_namespace(namespace_bytes, sizeof(namespace_bytes), &storage_namespace) ==
            StorageSerdeResult::Ok) {
            return storage_namespace.capacity;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return Error{ErrorCode::kProtocol, "timed out waiting for storage namespace response"};
}

}  // namespace nds::client
