#include "ra.hh"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace nds {
namespace {

Result<void> validate_context(const RaStorageContext &context) {
    if (context.connection.runtime == nullptr || context.connection.qp == nullptr ||
        context.transport_state == nullptr || context.command_device == nullptr ||
        context.completion_device == nullptr || context.command_buffer.address == 0U ||
        context.command_buffer.local_key == 0U || context.command_buffer.length < kStorageCommandBytes ||
        context.completion.address == 0U || context.completion.local_key == 0U ||
        context.completion.length < kStorageCompletionBytes || context.capacity == 0U ||
        context.storage_state == nullptr || context.storage_state->command_id == 0U ||
        context.storage_state->reserved != 0U)
        return Error{ErrorCode::kInvalidArgument, "invalid RA storage context"};
    return {};
}

Result<std::uint64_t> claim(const RaStorageContext &context, std::uint64_t expected_bytes) {
    NDS_RETURN_IF_ERROR(validate_context(context));
    if (expected_bytes == 0U || expected_bytes > UINT32_MAX || context.storage_state->in_flight != 0U)
        return Error{ErrorCode::kTransport, "RA storage slot is already in flight or has an invalid length"};
    context.storage_state->expected_bytes = static_cast<std::uint32_t>(expected_bytes);
    context.storage_state->in_flight = 1U;
    return context.storage_state->command_id;
}

void abandon(const RaStorageContext &context) {
    context.storage_state->expected_bytes = 0U;
    context.storage_state->in_flight = 0U;
}

template <typename Request, typename Serialize>
Result<void> execute(const RaStorageContext &context, const Request &command, Serialize serialize,
                     std::uint64_t expected_bytes) {
    NDS_ASSIGN_OR_RETURN(const std::uint64_t command_id, claim(context, expected_bytes));
    Request request = command;
    request.command_id = command_id;
    request.slot_index = context.slot_index;
    std::uint8_t pending[kStorageCompletionBytes]{};
    const StorageCompletion pending_completion{command_id, StorageCompletionState::Pending, StorageStatus::Success, 0U};
    if (serialize_storage_completion(pending_completion, pending, sizeof(pending)) != StorageSerdeResult::Ok) {
        abandon(context);
        return Error{ErrorCode::kProtocol, "invalid RA pending completion record"};
    }
    if (const auto copied =
            context.connection.runtime->copy_host_to_device(context.completion_device, pending, sizeof(pending));
        !copied.ok()) {
        abandon(context);
        return copied.error();
    }

    std::uint8_t command_bytes[kStorageCommandBytes]{};
    if (serialize(request, command_bytes, sizeof(command_bytes)) != StorageSerdeResult::Ok) {
        abandon(context);
        return Error{ErrorCode::kProtocol, "invalid RA storage command"};
    }
    if (const auto copied = context.connection.runtime->copy_host_to_device(context.command_device, command_bytes,
                                                                            sizeof(command_bytes));
        !copied.ok()) {
        abandon(context);
        return copied.error();
    }

    const NdsSendWr transfer{
        .wr_id = command_id,
        .opcode = NDS_WR_SEND,
        .flags = 0U,
        .local = {.address = context.command_buffer.address,
                  .length = kStorageCommandBytes,
                  .local_key = context.command_buffer.local_key},
        .remote_address = 0U,
        .remote_key = 0U,
    };
    if (const auto submitted = NdsRaRdmaSend(context.connection, context.transport_state, transfer); !submitted.ok()) {
        abandon(context);
        return submitted.error();
    }
    return {};
}

template <typename Entry, typename Deserialize>
Result<std::uint64_t> decode_entries(const RaStorageContext &context, const NdsStorageBatchOperationArgs &args,
                                     std::vector<Entry> *entries, Deserialize deserialize) {
    if (args.operation.entry_count == 0U || args.operation.entry_count > kStorageMaxBatchEntries ||
        args.operation.entries_address == 0U || args.operation.entries_key == 0U || entries == nullptr ||
        args.operation.entry_count > std::numeric_limits<std::size_t>::max() / kStorageBatchEntryBytes)
        return Error{ErrorCode::kInvalidArgument, "invalid RA storage batch entries"};
    const std::size_t bytes = static_cast<std::size_t>(args.operation.entry_count) * kStorageBatchEntryBytes;
    std::vector<std::uint8_t> encoded(bytes);
    NDS_RETURN_IF_ERROR(context.connection.runtime->copy_device_to_host(
        encoded.data(), reinterpret_cast<const void *>(args.operation.entries_address), bytes));
    entries->resize(args.operation.entry_count);
    std::uint64_t total = 0U;
    for (std::uint32_t index = 0U; index < args.operation.entry_count; ++index) {
        if (deserialize(encoded.data() + index * kStorageBatchEntryBytes, kStorageBatchEntryBytes,
                        &(*entries)[index]) != StorageSerdeResult::Ok)
            return Error{ErrorCode::kProtocol, "invalid RA storage batch entry"};
        const auto &entry = (*entries)[index];
        if (entry.length == 0U || entry.data.address == 0U || entry.data.remote_key == 0U ||
            entry.data.length < entry.length || entry.offset > context.capacity ||
            entry.length > context.capacity - entry.offset || entry.length > UINT32_MAX ||
            total > UINT32_MAX - entry.length)
            return Error{ErrorCode::kInvalidArgument, "RA storage batch entry exceeds its declared range"};
        total += entry.length;
    }
    return total;
}

}  // namespace

Result<void> NdsRaStorageBootstrap(const RaConnection &connection, NdsTransportQpState *state,
                                   const NdsSge &bootstrap) {
    if (connection.runtime == nullptr || connection.qp == nullptr || state == nullptr || bootstrap.address == 0U ||
        bootstrap.local_key == 0U || bootstrap.length < kStorageBootstrapBytes)
        return Error{ErrorCode::kInvalidArgument, "invalid RA storage bootstrap"};
    const NdsSendWr transfer{
        .wr_id = 1U,
        .opcode = NDS_WR_SEND,
        .flags = 0U,
        .local = bootstrap,
        .remote_address = 0U,
        .remote_key = 0U,
    };
    return NdsRaRdmaSend(connection, state, transfer);
}

Result<void> NdsRaStorageRead(const RaStorageContext &context, const NdsStorageOperationArgs &args) {
    if (args.operation.server_offset > context.capacity || args.operation.length == 0U ||
        args.operation.length > context.capacity - args.operation.server_offset ||
        args.operation.buffer_address == 0U || args.operation.buffer_key == 0U)
        return Error{ErrorCode::kInvalidArgument, "invalid RA storage read arguments"};
    const StorageReadCommand command{0U,
                                     args.operation.server_offset,
                                     args.operation.length,
                                     {args.operation.buffer_address, args.operation.length, args.operation.buffer_key},
                                     context.slot_index};
    return execute(context, command, serialize_storage_read, args.operation.length);
}

Result<void> NdsRaStorageWrite(const RaStorageContext &context, const NdsStorageOperationArgs &args) {
    if (args.operation.server_offset > context.capacity || args.operation.length == 0U ||
        args.operation.length > context.capacity - args.operation.server_offset ||
        args.operation.buffer_address == 0U || args.operation.buffer_key == 0U)
        return Error{ErrorCode::kInvalidArgument, "invalid RA storage write arguments"};
    const StorageWriteCommand command{0U,
                                      args.operation.server_offset,
                                      args.operation.length,
                                      {args.operation.buffer_address, args.operation.length, args.operation.buffer_key},
                                      context.slot_index};
    return execute(context, command, serialize_storage_write, args.operation.length);
}

Result<void> NdsRaStorageBatchRead(const RaStorageContext &context, const NdsStorageBatchOperationArgs &args) {
    std::vector<StorageBatchReadEntry> entries;
    NDS_ASSIGN_OR_RETURN(const std::uint64_t total,
                         decode_entries(context, args, &entries, deserialize_storage_batch_read_entry));
    const StorageBatchReadCommand command{
        0U,
        args.operation.entry_count,
        total,
        {args.operation.entries_address,
         static_cast<std::uint64_t>(args.operation.entry_count) * kStorageBatchEntryBytes, args.operation.entries_key},
        context.slot_index};
    return execute(context, command, serialize_storage_batch_read, total);
}

Result<void> NdsRaStorageBatchWrite(const RaStorageContext &context, const NdsStorageBatchOperationArgs &args) {
    std::vector<StorageBatchWriteEntry> entries;
    NDS_ASSIGN_OR_RETURN(const std::uint64_t total,
                         decode_entries(context, args, &entries, deserialize_storage_batch_write_entry));
    const StorageBatchWriteCommand command{
        0U,
        args.operation.entry_count,
        total,
        {args.operation.entries_address,
         static_cast<std::uint64_t>(args.operation.entry_count) * kStorageBatchEntryBytes, args.operation.entries_key},
        context.slot_index};
    return execute(context, command, serialize_storage_batch_write, total);
}

Result<StorageCompletion> NdsRaStorageWait(const RaStorageContext &context) {
    NDS_RETURN_IF_ERROR(validate_context(context));
    std::uint8_t bytes[kStorageCompletionBytes]{};
    NDS_RETURN_IF_ERROR(
        context.connection.runtime->copy_device_to_host(bytes, context.completion_device, sizeof(bytes)));
    StorageCompletion completion{};
    if (deserialize_storage_completion(bytes, sizeof(bytes), &completion) != StorageSerdeResult::Ok)
        return Error{ErrorCode::kProtocol, "invalid storage completion record"};
    if (completion.state != StorageCompletionState::Complete)
        return Error{ErrorCode::kTransport, "storage completion is pending"};
    if (completion.command_id != context.storage_state->command_id ||
        completion.bytes_transferred != context.storage_state->expected_bytes)
        return Error{ErrorCode::kProtocol, "storage completion does not match the slot state"};
    return completion;
}

}  // namespace nds
