#include "ra.hh"

namespace nds {
namespace {

Result<void> validate_context(const RaStorageContext &context) {
    if (context.connection.runtime == nullptr || context.connection.qp == nullptr ||
        context.connection.qp->backend_mode() != client::NpuBackend::Ra || context.command_device == nullptr ||
        context.completion_device == nullptr || context.command_buffer.address == 0U ||
        context.command_buffer.local_key == 0U || context.command_buffer.length < kStorageCommandBytes ||
        context.completion.address == 0U || context.completion.local_key == 0U ||
        context.completion.length < kStorageCompletionBytes || context.capacity == 0U)
        return unexpected(ErrorCode::kInvalidArgument, "invalid RA storage context");
    return {};
}

template <typename Request, typename Serialize>
Result<void> execute(const RaStorageContext &context, const Request &command, Serialize serialize) {
    if (const auto valid = validate_context(context); !valid)
        return unexpected(valid.error());
    uint8_t pending[kStorageCompletionBytes]{};
    const StorageCompletion pending_completion{command.command_id, StorageCompletionState::Pending,
                                               StorageStatus::Success, 0U};
    if (serialize_storage_completion(pending_completion, pending, sizeof(pending)) != StorageSerdeResult::Ok)
        return unexpected(ErrorCode::kProtocol, "invalid RA pending completion record");
    if (const auto copied =
            context.connection.runtime->copy_host_to_device(context.completion_device, pending, sizeof(pending));
        !copied)
        return unexpected(copied.error());

    uint8_t command_bytes[kStorageCommandBytes]{};
    if (serialize(command, command_bytes, sizeof(command_bytes)) != StorageSerdeResult::Ok)
        return unexpected(ErrorCode::kProtocol, "invalid RA storage command");
    if (const auto copied = context.connection.runtime->copy_host_to_device(context.command_device, command_bytes,
                                                                            sizeof(command_bytes));
        !copied)
        return unexpected(copied.error());

    const NdsDeviceSendWr transfer{
        command.command_id,
        NDS_DEVICE_WR_SEND,
        NDS_DEVICE_SEND_SIGNALED,
        {context.command_buffer.address, kStorageCommandBytes, context.command_buffer.local_key},
        0U,
        0U,
        0U};
    return NdsRaRdmaSend(context.connection, transfer);
}

}  // namespace

Result<void> NdsRaStorageRead(const RaStorageContext &context, const StorageReadCommand &command) {
    if (command.offset > context.capacity || command.length > context.capacity - command.offset)
        return unexpected(ErrorCode::kInvalidArgument, "RA storage read exceeds namespace capacity");
    return execute(context, command, serialize_storage_read);
}

Result<void> NdsRaStorageWrite(const RaStorageContext &context, const StorageWriteCommand &command) {
    if (command.offset > context.capacity || command.length > context.capacity - command.offset)
        return unexpected(ErrorCode::kInvalidArgument, "RA storage write exceeds namespace capacity");
    return execute(context, command, serialize_storage_write);
}

Result<void> NdsRaStorageBatchRead(const RaStorageContext &context, const StorageBatchReadCommand &command) {
    return execute(context, command, serialize_storage_batch_read);
}

Result<void> NdsRaStorageBatchWrite(const RaStorageContext &context, const StorageBatchWriteCommand &command) {
    return execute(context, command, serialize_storage_batch_write);
}

}  // namespace nds
