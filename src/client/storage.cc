#include "storage.hh"

#include "backends/launcher.hh"

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
constexpr std::uint32_t kStorageSlotsPerQp = 4U;

struct StreamOwner {
    aclrtStream stream{};

    ~StreamOwner() {
        if (stream != nullptr)
            (void)aclrtDestroyStream(stream);
    }
};

Result<void> submit_transport_bootstrap(Launcher *launcher, Transport *transport, const MemoryRegion &region,
                                        std::uint32_t length) {
    if (launcher == nullptr || transport == nullptr)
        return Error{ErrorCode::kInvalidArgument, "storage bootstrap requires a launcher and transport"};
    StreamOwner stream;
    if (transport->backend().mode != BackendMode::Ra) {
        const int result = aclrtCreateStream(&stream.stream);
        if (result != ACL_SUCCESS || stream.stream == nullptr)
            return Error{ErrorCode::kRuntime, "storage bootstrap stream creation failed: " + std::to_string(result)};
    }
    const NdsSendWr wr{
        .wr_id = 1U,
        .opcode = NDS_WR_SEND,
        .flags = 0U,
        .local = {.address = region.address(), .length = length, .local_key = region.local_key()},
        .remote_address = 0U,
        .remote_key = 0U,
    };
    return launcher->with_config({.stream = stream.stream, .sync = true, .sync_timeout_ms = kCompletionTimeoutMs})
        .rdma_send(transport->device_transport(), 0U, wr);
}

}  // namespace

StorageClient::~StorageClient() = default;

Result<void> StorageClient::open(Runtime *runtime, Transport *transport) {
    if (opened_ || runtime == nullptr || transport == nullptr || !runtime->initialized())
        return Error{ErrorCode::kInvalidArgument, "storage client requires one open runtime and transport"};
    if (transport->qp_count() == 0U)
        return Error{ErrorCode::kInvalidArgument, "storage client requires at least one transport QP"};

    runtime_ = runtime;
    transport_ = transport;
    const auto register_region = [this](const MemoryBuffer &buffer, MemoryAccess access,
                                        const char *name) -> Result<MemoryRegion> {
        auto registered = transport_->register_memory(buffer, access);
        if (!registered.ok())
            return Error{registered.error().code,
                         std::string(name) + " registration failed: " + registered.error().message};
        return std::move(registered).value();
    };
    if (transport_->qp_count() > std::numeric_limits<std::size_t>::max() / kStorageSlotsPerQp)
        return Error{ErrorCode::kInvalidArgument, "storage slot count overflows"};
    slots_.resize(transport_->qp_count() * kStorageSlotsPerQp);
    pending_.resize(slots_.size());
    if (slots_.size() > std::numeric_limits<std::size_t>::max() / kStorageCommandBytes ||
        slots_.size() > std::numeric_limits<std::size_t>::max() / kStorageCompletionBytes) {
        return Error{ErrorCode::kInvalidArgument, "storage slot allocation size overflows"};
    }
    NDS_ASSIGN_OR_RETURN(bootstrap_buffer_, runtime_->allocate(kStorageBootstrapBytes, MemoryLocation::Device));
    NDS_ASSIGN_OR_RETURN(bootstrap_region_,
                         register_region(bootstrap_buffer_, MemoryAccess::LocalWrite | MemoryAccess::RemoteRead,
                                         "storage bootstrap buffer"));
    NDS_ASSIGN_OR_RETURN(command_buffer_,
                         runtime_->allocate(slots_.size() * kStorageCommandBytes, MemoryLocation::Device));
    NDS_ASSIGN_OR_RETURN(command_region_, register_region(command_buffer_,
                                                          MemoryAccess::LocalWrite | MemoryAccess::RemoteWrite |
                                                              MemoryAccess::RemoteRead,
                                                          "storage command buffer"));
    NDS_ASSIGN_OR_RETURN(completion_buffer_,
                         runtime_->allocate(slots_.size() * kStorageCompletionBytes, MemoryLocation::Device));
    NDS_ASSIGN_OR_RETURN(completion_region_, register_region(completion_buffer_,
                                                             MemoryAccess::LocalWrite | MemoryAccess::RemoteWrite |
                                                                 MemoryAccess::RemoteRead,
                                                             "storage completion buffer"));
    for (std::size_t index = 0U; index < slots_.size(); ++index) {
        slots_[index].command_offset = index * kStorageCommandBytes;
        slots_[index].completion_offset = index * kStorageCompletionBytes;
        slots_[index].qp_index = static_cast<std::uint32_t>(index % transport_->qp_count());
    }

    NDS_ASSIGN_OR_RETURN(namespace_buffer_, runtime_->allocate(kStorageNamespaceBytes, MemoryLocation::Device));
    NDS_ASSIGN_OR_RETURN(namespace_region_, register_region(namespace_buffer_,
                                                            MemoryAccess::LocalWrite | MemoryAccess::RemoteWrite |
                                                                MemoryAccess::RemoteRead,
                                                            "storage namespace buffer"));

    const MemoryLocation descriptor_location =
        transport_->backend().mode == BackendMode::Ra ? MemoryLocation::Host : MemoryLocation::Device;
    const std::size_t descriptor_bytes = slots_.size() * sizeof(NdsStorageSlotDescriptor);
    NDS_ASSIGN_OR_RETURN(slot_descriptors_buffer_, runtime_->allocate(descriptor_bytes, descriptor_location));
    std::vector<NdsStorageSlotDescriptor> descriptors(slots_.size());
    for (std::size_t index = 0U; index < slots_.size(); ++index) {
        descriptors[index] = NdsStorageSlotDescriptor{
            .command_buffer = {.address = command_region_.address() + slots_[index].command_offset,
                               .length = kStorageCommandBytes,
                               .local_key = command_region_.local_key()},
            .completion_buffer = {.address = completion_region_.address() + slots_[index].completion_offset,
                                  .length = kStorageCompletionBytes,
                                  .local_key = completion_region_.remote_key()},
            .qp_index = slots_[index].qp_index,
            .reserved = 0U,
        };
    }
    if (const auto copied = runtime_->copy_to(&slot_descriptors_buffer_, descriptors.data(), descriptor_bytes);
        !copied.ok())
        return Error{copied.error().code, "storage slot-descriptor copy failed: " + copied.error().message};

    const std::size_t slot_table_bytes = slots_.size() * sizeof(StorageSlot);
    const MemoryLocation slot_table_location =
        transport_->backend().mode == BackendMode::Ra ? MemoryLocation::HostPinned : MemoryLocation::Device;
    NDS_ASSIGN_OR_RETURN(slot_table_buffer_, runtime_->allocate(slot_table_bytes, slot_table_location));
    std::vector<StorageSlot> slot_table(slots_.size());
    for (std::size_t index = 0U; index < slots_.size(); ++index) {
        slot_table[index] = StorageSlot{
            .command = {command_region_.address() + slots_[index].command_offset, kStorageCommandBytes,
                        command_region_.remote_key()},
            .completion = {completion_region_.address() + slots_[index].completion_offset, kStorageCompletionBytes,
                           completion_region_.remote_key()},
            .qp_index = slots_[index].qp_index,
            .reserved = 0U,
        };
    }
    if (const auto copied = runtime_->copy_to(&slot_table_buffer_, slot_table.data(), slot_table_bytes); !copied.ok())
        return Error{copied.error().code, "storage slot-table copy failed: " + copied.error().message};
    NDS_ASSIGN_OR_RETURN(
        slot_table_region_,
        register_region(slot_table_buffer_, MemoryAccess::LocalWrite | MemoryAccess::RemoteRead, "storage slot table"));

    NDS_ASSIGN_OR_RETURN(launcher_,
                         Launcher::open(runtime_, transport_->backend().mode, transport_->backend().artifact_path));
    NDS_ASSIGN_OR_RETURN(capacity_, exchange_bootstrap());
    storage_descriptor_ = NdsStorageDescriptor{
        .transport = transport_->device_transport(),
        .slot_descriptors_address = reinterpret_cast<std::uint64_t>(slot_descriptors_buffer_.data()),
        .slot_count = static_cast<std::uint32_t>(slots_.size()),
        .reserved = 0U,
        .capacity = capacity_,
    };
    const nds::QpInfo &local = transport_->local_qp_info();
    next_command_id_ = (static_cast<std::uint64_t>(local.qp_num) << 32U) | local.psn;
    if (next_command_id_ == 0U)
        next_command_id_ = 1U;
    opened_ = true;
    return {};
}

Result<StorageCompletionHandle> StorageClient::read(std::uint64_t offset, MemoryBuffer *data, std::uint32_t length) {
    if (!opened_)
        return Error{ErrorCode::kInvalidArgument, "storage submission requires an open client"};
    if (const auto valid = validate_io({offset, data, length}); !valid.ok())
        return Error{valid.error()};
    NDS_ASSIGN_OR_RETURN(const std::uint32_t slot_index, acquire_slot());
    NDS_ASSIGN_OR_RETURN(MemoryRegion region,
                         transport_->register_memory(
                             *data, MemoryAccess::LocalWrite | MemoryAccess::RemoteWrite | MemoryAccess::RemoteRead));
    const std::uint64_t command_id = allocate_command_id();
    PendingRequest pending{
        .command_id = command_id,
        .expected_bytes = length,
        .data_regions = {},
        .descriptor_buffer = {},
        .descriptor_region = {},
    };
    pending.data_regions.push_back(std::move(region));
    const StorageReadCommand command{
        .command_id = command_id,
        .offset = offset,
        .length = length,
        .data = {pending.data_regions.front().address(), pending.data_regions.front().length(),
                 pending.data_regions.front().remote_key()},
        .slot_index = slot_index,
    };
    if (const auto submitted = execute_storage_read(command); !submitted.ok())
        return Error{submitted.error()};
    pending_[slot_index] = std::move(pending);
    return StorageCompletionHandle{command_id, slot_index};
}

Result<StorageCompletionHandle> StorageClient::write(std::uint64_t offset, MemoryBuffer *data, std::uint32_t length) {
    if (!opened_)
        return Error{ErrorCode::kInvalidArgument, "storage submission requires an open client"};
    if (const auto valid = validate_io({offset, data, length}); !valid.ok())
        return Error{valid.error()};
    NDS_ASSIGN_OR_RETURN(const std::uint32_t slot_index, acquire_slot());
    NDS_ASSIGN_OR_RETURN(MemoryRegion region,
                         transport_->register_memory(
                             *data, MemoryAccess::LocalWrite | MemoryAccess::RemoteWrite | MemoryAccess::RemoteRead));
    const std::uint64_t command_id = allocate_command_id();
    PendingRequest pending{
        .command_id = command_id,
        .expected_bytes = length,
        .data_regions = {},
        .descriptor_buffer = {},
        .descriptor_region = {},
    };
    pending.data_regions.push_back(std::move(region));
    const StorageWriteCommand command{
        .command_id = command_id,
        .offset = offset,
        .length = length,
        .data = {pending.data_regions.front().address(), pending.data_regions.front().length(),
                 pending.data_regions.front().remote_key()},
        .slot_index = slot_index,
    };
    if (const auto submitted = execute_storage_write(command); !submitted.ok())
        return Error{submitted.error()};
    pending_[slot_index] = std::move(pending);
    return StorageCompletionHandle{command_id, slot_index};
}

Result<StorageCompletionHandle> StorageClient::read_batch(std::span<const StorageIo> requests) {
    if (!opened_)
        return Error{ErrorCode::kInvalidArgument, "storage submission requires an open client"};
    if (requests.empty() || requests.size() > kStorageMaxBatchEntries)
        return Error{ErrorCode::kInvalidArgument, "storage batch read has an invalid entry count"};
    NDS_ASSIGN_OR_RETURN(const std::uint32_t slot_index, acquire_slot());
    PendingRequest pending{};
    pending.command_id = allocate_command_id();
    pending.data_regions.reserve(requests.size());
    std::uint64_t total_length{};
    for (const StorageIo &request : requests) {
        if (const auto valid = validate_io(request); !valid.ok())
            return Error{valid.error()};
        if (request.length > std::numeric_limits<std::uint64_t>::max() - total_length)
            return Error{ErrorCode::kInvalidArgument, "storage batch read byte count overflows"};
        total_length += request.length;
        NDS_ASSIGN_OR_RETURN(
            MemoryRegion region,
            transport_->register_memory(
                *request.data, MemoryAccess::LocalWrite | MemoryAccess::RemoteWrite | MemoryAccess::RemoteRead));
        pending.data_regions.push_back(std::move(region));
    }
    std::vector<std::uint8_t> bytes(requests.size() * kStorageBatchEntryBytes);
    for (std::size_t index = 0U; index < requests.size(); ++index) {
        const StorageBatchReadEntry entry{
            .offset = requests[index].offset,
            .length = requests[index].length,
            .data = {pending.data_regions[index].address(), pending.data_regions[index].length(),
                     pending.data_regions[index].remote_key()},
        };
        if (serialize_storage_batch_read_entry(entry, bytes.data() + index * kStorageBatchEntryBytes,
                                               kStorageBatchEntryBytes) != StorageSerdeResult::Ok)
            return Error{ErrorCode::kProtocol, "invalid storage batch-read entry"};
    }
    NDS_ASSIGN_OR_RETURN(pending.descriptor_buffer, runtime_->allocate(bytes.size(), MemoryLocation::Device));
    NDS_RETURN_IF_ERROR(runtime_->copy_to(&pending.descriptor_buffer, bytes.data(), bytes.size()));
    NDS_ASSIGN_OR_RETURN(
        pending.descriptor_region,
        transport_->register_memory(pending.descriptor_buffer,
                                    MemoryAccess::LocalWrite | MemoryAccess::RemoteWrite | MemoryAccess::RemoteRead));
    pending.expected_bytes = total_length;
    const StorageBatchReadCommand command{
        .command_id = pending.command_id,
        .entry_count = requests.size(),
        .total_length = total_length,
        .entries = {pending.descriptor_region.address(), pending.descriptor_region.length(),
                    pending.descriptor_region.remote_key()},
        .slot_index = slot_index,
    };
    if (const auto submitted = execute_storage_batch_read(command); !submitted.ok())
        return Error{submitted.error()};
    const std::uint64_t command_id = pending.command_id;
    pending_[slot_index] = std::move(pending);
    return StorageCompletionHandle{command_id, slot_index};
}

Result<StorageCompletionHandle> StorageClient::write_batch(std::span<const StorageIo> requests) {
    if (!opened_)
        return Error{ErrorCode::kInvalidArgument, "storage submission requires an open client"};
    if (requests.empty() || requests.size() > kStorageMaxBatchEntries)
        return Error{ErrorCode::kInvalidArgument, "storage batch write has an invalid entry count"};
    NDS_ASSIGN_OR_RETURN(const std::uint32_t slot_index, acquire_slot());
    PendingRequest pending{};
    pending.command_id = allocate_command_id();
    pending.data_regions.reserve(requests.size());
    std::uint64_t total_length{};
    for (const StorageIo &request : requests) {
        if (const auto valid = validate_io(request); !valid.ok())
            return Error{valid.error()};
        if (request.length > std::numeric_limits<std::uint64_t>::max() - total_length)
            return Error{ErrorCode::kInvalidArgument, "storage batch write byte count overflows"};
        total_length += request.length;
        NDS_ASSIGN_OR_RETURN(
            MemoryRegion region,
            transport_->register_memory(
                *request.data, MemoryAccess::LocalWrite | MemoryAccess::RemoteWrite | MemoryAccess::RemoteRead));
        pending.data_regions.push_back(std::move(region));
    }
    std::vector<std::uint8_t> bytes(requests.size() * kStorageBatchEntryBytes);
    for (std::size_t index = 0U; index < requests.size(); ++index) {
        const StorageBatchWriteEntry entry{
            .offset = requests[index].offset,
            .length = requests[index].length,
            .data = {pending.data_regions[index].address(), pending.data_regions[index].length(),
                     pending.data_regions[index].remote_key()},
        };
        if (serialize_storage_batch_write_entry(entry, bytes.data() + index * kStorageBatchEntryBytes,
                                                kStorageBatchEntryBytes) != StorageSerdeResult::Ok)
            return Error{ErrorCode::kProtocol, "invalid storage batch-write entry"};
    }
    NDS_ASSIGN_OR_RETURN(pending.descriptor_buffer, runtime_->allocate(bytes.size(), MemoryLocation::Device));
    NDS_RETURN_IF_ERROR(runtime_->copy_to(&pending.descriptor_buffer, bytes.data(), bytes.size()));
    NDS_ASSIGN_OR_RETURN(
        pending.descriptor_region,
        transport_->register_memory(pending.descriptor_buffer,
                                    MemoryAccess::LocalWrite | MemoryAccess::RemoteWrite | MemoryAccess::RemoteRead));
    pending.expected_bytes = total_length;
    const StorageBatchWriteCommand command{
        .command_id = pending.command_id,
        .entry_count = requests.size(),
        .total_length = total_length,
        .entries = {pending.descriptor_region.address(), pending.descriptor_region.length(),
                    pending.descriptor_region.remote_key()},
        .slot_index = slot_index,
    };
    if (const auto submitted = execute_storage_batch_write(command); !submitted.ok())
        return Error{submitted.error()};
    const std::uint64_t command_id = pending.command_id;
    pending_[slot_index] = std::move(pending);
    return StorageCompletionHandle{command_id, slot_index};
}

std::uint64_t StorageClient::capacity() const noexcept {
    return capacity_;
}

std::size_t StorageClient::slot_count() const noexcept {
    return slots_.size();
}

Result<void> StorageClient::validate_io(const StorageIo &request) const {
    if (request.data == nullptr || request.length == 0U || request.length > request.data->size())
        return Error{ErrorCode::kInvalidArgument, "storage operation requires a sufficiently large data buffer"};
    if (request.offset > capacity_ || request.length > capacity_ - request.offset)
        return Error{ErrorCode::kProtocol, "requested storage range exceeds namespace capacity"};
    return {};
}

Result<std::uint32_t> StorageClient::acquire_slot() {
    if (slots_.empty() || pending_.size() != slots_.size())
        return Error{ErrorCode::kInvalidArgument, "storage client has no available slots"};
    for (std::size_t attempt = 0U; attempt < slots_.size(); ++attempt) {
        const std::size_t candidate = (next_slot_ + attempt) % slots_.size();
        if (!pending_[candidate].has_value()) {
            next_slot_ = (candidate + 1U) % slots_.size();
            return static_cast<std::uint32_t>(candidate);
        }
    }
    return Error{ErrorCode::kTransport, "all storage slots have a command in flight"};
}

Result<void> StorageClient::wait(StorageCompletionHandle handle, std::uint32_t timeout_ms) {
    if (!opened_ || handle.slot_index >= pending_.size() || !pending_[handle.slot_index].has_value() ||
        handle.command_id == 0U || pending_[handle.slot_index]->command_id != handle.command_id)
        return Error{ErrorCode::kInvalidArgument, "storage wait requires a current completion handle"};
    if (timeout_ms == 0U || timeout_ms > static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max()))
        return Error{ErrorCode::kInvalidArgument, "storage wait requires a positive timeout"};
    const PendingRequest &pending = pending_[handle.slot_index].value();
    const auto completion =
        observe_completion(handle.slot_index, handle.command_id, pending.expected_bytes, timeout_ms);
    if (!completion.ok())
        return Error{completion.error()};
    pending_[handle.slot_index].reset();
    if (completion.value().status != StorageStatus::Success)
        return Error{ErrorCode::kProtocol, "storage completion returned a failure status"};
    return {};
}

Result<StorageCompletion> StorageClient::observe_completion(std::uint32_t slot_index, std::uint64_t command_id,
                                                            std::uint64_t expected_bytes, std::uint32_t timeout_ms) {
    if (slot_index >= slots_.size())
        return Error{ErrorCode::kInvalidArgument, "storage completion slot is out of range"};
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (std::chrono::steady_clock::now() < deadline) {
        std::uint8_t bytes[kStorageCompletionBytes]{};
        const auto *completion_address =
            static_cast<const std::uint8_t *>(completion_buffer_.data()) + slots_[slot_index].completion_offset;
        if (const auto copied = runtime_->copy_device_to_host(bytes, completion_address, sizeof(bytes)); !copied.ok())
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
    if (const auto ready = transport_->ready(); !ready.ok())
        return Error{ready.error()};
    if (launcher_ == nullptr)
        return Error{ErrorCode::kInvalidArgument, "storage requires an open launcher"};
    StreamOwner stream;
    if (transport_->backend().mode != BackendMode::Ra) {
        const int result = aclrtCreateStream(&stream.stream);
        if (result != ACL_SUCCESS || stream.stream == nullptr)
            return Error{ErrorCode::kRuntime, "storage Read stream creation failed: " + std::to_string(result)};
    }
    return launcher_->with_config({.stream = stream.stream, .sync = true, .sync_timeout_ms = kCompletionTimeoutMs})
        .storage_read(storage_descriptor_, command);
}

Result<void> StorageClient::execute_storage_write(const StorageWriteCommand &command) {
    if (const auto ready = transport_->ready(); !ready.ok())
        return Error{ready.error()};
    if (launcher_ == nullptr)
        return Error{ErrorCode::kInvalidArgument, "storage requires an open launcher"};
    StreamOwner stream;
    if (transport_->backend().mode != BackendMode::Ra) {
        const int result = aclrtCreateStream(&stream.stream);
        if (result != ACL_SUCCESS || stream.stream == nullptr)
            return Error{ErrorCode::kRuntime, "storage Write stream creation failed: " + std::to_string(result)};
    }
    return launcher_->with_config({.stream = stream.stream, .sync = true, .sync_timeout_ms = kCompletionTimeoutMs})
        .storage_write(storage_descriptor_, command);
}

Result<void> StorageClient::execute_storage_batch_read(const StorageBatchReadCommand &command) {
    if (const auto ready = transport_->ready(); !ready.ok())
        return Error{ready.error()};
    if (launcher_ == nullptr)
        return Error{ErrorCode::kInvalidArgument, "storage requires an open launcher"};
    StreamOwner stream;
    if (transport_->backend().mode != BackendMode::Ra) {
        const int result = aclrtCreateStream(&stream.stream);
        if (result != ACL_SUCCESS || stream.stream == nullptr)
            return Error{ErrorCode::kRuntime, "storage batch Read stream creation failed: " + std::to_string(result)};
    }
    return launcher_->with_config({.stream = stream.stream, .sync = true, .sync_timeout_ms = kCompletionTimeoutMs})
        .storage_read_batch(storage_descriptor_, command);
}

Result<void> StorageClient::execute_storage_batch_write(const StorageBatchWriteCommand &command) {
    if (const auto ready = transport_->ready(); !ready.ok())
        return Error{ready.error()};
    if (launcher_ == nullptr)
        return Error{ErrorCode::kInvalidArgument, "storage requires an open launcher"};
    StreamOwner stream;
    if (transport_->backend().mode != BackendMode::Ra) {
        const int result = aclrtCreateStream(&stream.stream);
        if (result != ACL_SUCCESS || stream.stream == nullptr)
            return Error{ErrorCode::kRuntime, "storage batch Write stream creation failed: " + std::to_string(result)};
    }
    return launcher_->with_config({.stream = stream.stream, .sync = true, .sync_timeout_ms = kCompletionTimeoutMs})
        .storage_write_batch(storage_descriptor_, command);
}

Result<std::uint64_t> StorageClient::exchange_bootstrap() {
    if (slots_.empty())
        return Error{ErrorCode::kInvalidArgument, "storage bootstrap requires a slot"};
    const StorageBootstrap bootstrap{
        .completion = {completion_region_.address(), kStorageCompletionBytes, completion_region_.remote_key()},
        .namespace_response = {namespace_region_.address(), namespace_region_.length(), namespace_region_.remote_key()},
        .slots = {slot_table_region_.address(), slot_table_region_.length(), slot_table_region_.remote_key()},
        .slot_count = static_cast<std::uint32_t>(slots_.size()),
    };
    std::uint8_t bootstrap_bytes[kStorageBootstrapBytes]{};
    std::uint8_t namespace_bytes[kStorageNamespaceBytes]{};
    StorageNamespace storage_namespace{};
    if (serialize_storage_bootstrap(bootstrap, bootstrap_bytes, sizeof(bootstrap_bytes)) != StorageSerdeResult::Ok)
        return Error{ErrorCode::kProtocol, "invalid storage bootstrap record"};
    if (const auto copied = runtime_->copy_to(&namespace_buffer_, namespace_bytes, sizeof(namespace_bytes));
        !copied.ok())
        return Error{copied.error().code, "storage namespace initialization failed: " + copied.error().message};
    if (const auto copied = runtime_->copy_to(&bootstrap_buffer_, bootstrap_bytes, sizeof(bootstrap_bytes));
        !copied.ok())
        return Error{copied.error().code, "storage bootstrap initialization failed: " + copied.error().message};
    if (const auto ready = transport_->ready(); !ready.ok())
        return Error{ready.error()};
    NDS_RETURN_IF_ERROR(
        submit_transport_bootstrap(launcher_.get(), transport_, bootstrap_region_, kStorageBootstrapBytes));

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(kCompletionTimeoutMs);
    while (std::chrono::steady_clock::now() < deadline) {
        if (const auto copied = runtime_->copy_from(namespace_bytes, namespace_buffer_, sizeof(namespace_bytes));
            !copied.ok())
            return Error{copied.error()};
        if (deserialize_storage_namespace(namespace_bytes, sizeof(namespace_bytes), &storage_namespace) ==
            StorageSerdeResult::Ok)
            return storage_namespace.capacity;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return Error{ErrorCode::kProtocol, "timed out waiting for storage namespace response"};
}

}  // namespace nds::client
