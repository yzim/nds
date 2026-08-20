#include "ra.hh"

#include <chrono>
#include <thread>

namespace nds {
namespace {

constexpr std::uint32_t kCompletionTimeoutMs = 5000U;

Result<void> validate_context(const RaStorageContext &context) {
    if (context.connection.runtime == nullptr || context.connection.qp == nullptr ||
        context.connection.qp->execution_mode() != client::NpuExecutionMode::Ra || context.command_device == nullptr ||
        context.completion_device == nullptr || context.command_buffer.address == 0U || context.command_buffer.local_key == 0U ||
        context.command_buffer.length < kStorageCommandBytes || context.completion.address == 0U ||
        context.completion.local_key == 0U || context.completion.length < kStorageCompletionBytes ||
        context.capacity == 0U)
        return unexpected(ErrorCode::kInvalidArgument, "invalid RA storage context");
    return {};
}

template <typename Request, typename Serialize>
Result<void> execute(const RaStorageContext &context, const Request &command, std::uint64_t expected_bytes,
                     Serialize serialize) {
    if (const auto valid = validate_context(context); !valid)
        return unexpected(valid.error());
    uint8_t pending[kStorageCompletionBytes]{};
    const StorageCompletion pending_completion{command.command_id, StorageCompletionState::Pending,
                                               StorageStatus::Success, 0U};
    if (serialize_storage_completion(pending_completion, pending, sizeof(pending)) != StorageSerdeResult::Ok)
        return unexpected(ErrorCode::kProtocol, "invalid RA pending completion record");
    if (const auto copied = context.connection.runtime->copy_host_to_device(context.completion_device, pending,
                                                                            sizeof(pending));
        !copied)
        return unexpected(copied.error());

    uint8_t command_bytes[kStorageCommandBytes]{};
    if (serialize(command, command_bytes, sizeof(command_bytes)) != StorageSerdeResult::Ok)
        return unexpected(ErrorCode::kProtocol, "invalid RA storage command");
    if (const auto copied =
            context.connection.runtime->copy_host_to_device(context.command_device, command_bytes,
                                                            sizeof(command_bytes));
        !copied)
        return unexpected(copied.error());

    const NdsDeviceTransfer transfer{command.command_id,
                                     {context.command_buffer.address, kStorageCommandBytes, context.command_buffer.local_key},
                                     0U,
                                     0U,
                                     0U};
    if (const auto posted = NdsRaRdmaSend(context.connection, transfer); !posted)
        return unexpected(posted.error());

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(kCompletionTimeoutMs);
    while (std::chrono::steady_clock::now() < deadline) {
        uint8_t bytes[kStorageCompletionBytes]{};
        if (const auto copied =
                context.connection.runtime->copy_device_to_host(bytes, context.completion_device, sizeof(bytes));
            !copied)
            return unexpected(copied.error());
        StorageCompletion completion{};
        if (deserialize_storage_completion(bytes, sizeof(bytes), &completion) != StorageSerdeResult::Ok)
            return unexpected(ErrorCode::kProtocol, "invalid RA storage completion record");
        if (completion.state == StorageCompletionState::Complete) {
            if (completion.command_id != command.command_id || completion.status != StorageStatus::Success ||
                completion.bytes_transferred != expected_bytes)
                return unexpected(ErrorCode::kProtocol, "RA storage completion does not match command");
            return {};
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return unexpected(ErrorCode::kProtocol, "timed out waiting for RA storage completion");
}

}  // namespace

Result<void> NdsRaStorageRead(const RaStorageContext &context, const StorageReadCommand &command) {
    if (command.offset > context.capacity || command.length > context.capacity - command.offset)
        return unexpected(ErrorCode::kInvalidArgument, "RA storage read exceeds namespace capacity");
    return execute(context, command, command.length, serialize_storage_read);
}

Result<void> NdsRaStorageWrite(const RaStorageContext &context, const StorageWriteCommand &command) {
    if (command.offset > context.capacity || command.length > context.capacity - command.offset)
        return unexpected(ErrorCode::kInvalidArgument, "RA storage write exceeds namespace capacity");
    return execute(context, command, command.length, serialize_storage_write);
}

Result<void> NdsRaStorageBatchRead(const RaStorageContext &context, const StorageBatchReadCommand &command) {
    return execute(context, command, command.total_length, serialize_storage_batch_read);
}

Result<void> NdsRaStorageBatchWrite(const RaStorageContext &context, const StorageBatchWriteCommand &command) {
    return execute(context, command, command.total_length, serialize_storage_batch_write);
}

}  // namespace nds
