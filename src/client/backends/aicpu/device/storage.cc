#include "api.h"
#include "internal.h"
#include "backend_storage.h"

namespace {

template <typename Request, typename Serialize>
uint32_t execute(const NdsStorageDescriptor *descriptor, uint32_t slot_id, uint32_t expected_bytes,
                 const Request &command, Serialize serialize, int32_t *return_value) {
    if (return_value == nullptr || !nds_storage_descriptor_valid(descriptor) ||
        !nds_storage_slot_id_valid(descriptor, slot_id) || expected_bytes == 0U)
        return kNdsAicpuInvalidArgument;
    const uint32_t slot_index = nds_storage_slot_id_index(slot_id);
    NdsStorageState *state = nds_storage_state(descriptor, slot_index);
    const NdsStorageSlotDescriptor *slot = nds_storage_slot(descriptor, slot_index);
    if (state == nullptr || slot == nullptr || state->command_id == 0U || state->in_flight != 0U)
        return kNdsAicpuInvalidArgument;
    state->expected_bytes = expected_bytes;
    state->in_flight = 1U;
    uint8_t pending[nds::kStorageCompletionBytes]{};
    uint8_t command_bytes[nds::kStorageCommandBytes]{};
    Request local = command;
    local.command_id = state->command_id;
    local.slot_index = slot_index;
    const nds::StorageCompletion pending_completion{state->command_id, nds::StorageCompletionState::Pending,
                                                    nds::StorageStatus::Success, 0U};
    if (nds::serialize_storage_completion(pending_completion, pending, sizeof(pending)) !=
            nds::StorageSerdeResult::Ok ||
        serialize(local, command_bytes, sizeof(command_bytes)) != nds::StorageSerdeResult::Ok) {
        state->in_flight = 0U;
        NdsAicpuSetReturnValue(return_value, NDS_OPERATION_INVALID_ARGUMENT);
        return kNdsAicpuSuccess;
    }
    auto *completion_address = reinterpret_cast<uint8_t *>(slot->completion_buffer.address);
    auto *command_address = reinterpret_cast<uint8_t *>(slot->command_buffer.address);
    for (uint32_t index = 0U; index < sizeof(pending); ++index) completion_address[index] = pending[index];
    for (uint32_t index = 0U; index < sizeof(command_bytes); ++index) command_address[index] = command_bytes[index];
    NdsAicpuBarrier();
    const NdsSendWr transfer{.wr_id = state->command_id,
                             .opcode = NDS_WR_SEND,
                             .flags = 0U,
                             .local = {.address = slot->command_buffer.address,
                                       .length = nds::kStorageCommandBytes,
                                       .local_key = slot->command_buffer.local_key},
                             .remote_address = 0U,
                             .remote_key = 0U};
    return nds_aicpu_rdma_send(&descriptor->transport, slot->qp_index, &transfer, return_value);
}

uint32_t batch_bytes(const NdsStorageBatchOperationArgs *args, bool write) {
    if (args == nullptr || args->operation.entries_address == 0U || args->operation.entries_key == 0U ||
        args->operation.entry_count == 0U || args->operation.entry_count > nds::kStorageMaxBatchEntries)
        return 0U;
    uint64_t total = 0U;
    for (uint32_t index = 0U; index < args->operation.entry_count; ++index) {
        const auto *bytes =
            reinterpret_cast<const uint8_t *>(args->operation.entries_address) + index * nds::kStorageBatchEntryBytes;
        uint64_t length = 0U;
        if (write) {
            nds::StorageBatchWriteEntry entry{};
            if (nds::deserialize_storage_batch_write_entry(bytes, nds::kStorageBatchEntryBytes, &entry) !=
                nds::StorageSerdeResult::Ok)
                return 0U;
            length = entry.length;
        } else {
            nds::StorageBatchReadEntry entry{};
            if (nds::deserialize_storage_batch_read_entry(bytes, nds::kStorageBatchEntryBytes, &entry) !=
                nds::StorageSerdeResult::Ok)
                return 0U;
            length = entry.length;
        }
        if (length == 0U || total > UINT32_MAX - length)
            return 0U;
        total += length;
    }
    return total > UINT32_MAX ? 0U : static_cast<uint32_t>(total);
}

}  // namespace

extern "C" uint32_t nds_aicpu_storage_bootstrap(const NdsStorageBootstrapDescriptor *bootstrap, int32_t *return_value) {
    if (bootstrap == nullptr || bootstrap->bootstrap.address == 0U || bootstrap->bootstrap.local_key == 0U ||
        bootstrap->bootstrap.length < nds::kStorageBootstrapBytes)
        return kNdsAicpuInvalidArgument;
    const NdsQpDescriptor *qp = nds_transport_qp(&bootstrap->transport, 0U);
    NdsTransportQpState *state = nds_transport_qp_state(&bootstrap->transport, 0U);
    if (qp == nullptr || state == nullptr)
        return kNdsAicpuInvalidArgument;
    const NdsSendWr transfer{.wr_id = 1U,
                             .opcode = NDS_WR_SEND,
                             .flags = 0U,
                             .local = bootstrap->bootstrap,
                             .remote_address = 0U,
                             .remote_key = 0U};
    return nds_aicpu_rdma_send(&bootstrap->transport, 0U, &transfer, return_value);
}

extern "C" uint32_t nds_aicpu_storage_read(const NdsStorageOperationArgs *args, int32_t *return_value) {
    if (args == nullptr || args->operation.length == 0U || args->operation.buffer_address == 0U ||
        args->operation.buffer_key == 0U)
        return kNdsAicpuInvalidArgument;
    const nds::StorageReadCommand command{
        0U,
        args->operation.server_offset,
        args->operation.length,
        {args->operation.buffer_address, args->operation.length, args->operation.buffer_key},
        0U};
    return execute(&args->storage, args->operation.slot_id, args->operation.length, command,
                   nds::serialize_storage_read, return_value);
}

extern "C" uint32_t nds_aicpu_storage_write(const NdsStorageOperationArgs *args, int32_t *return_value) {
    if (args == nullptr || args->operation.length == 0U || args->operation.buffer_address == 0U ||
        args->operation.buffer_key == 0U)
        return kNdsAicpuInvalidArgument;
    const nds::StorageWriteCommand command{
        0U,
        args->operation.server_offset,
        args->operation.length,
        {args->operation.buffer_address, args->operation.length, args->operation.buffer_key},
        0U};
    return execute(&args->storage, args->operation.slot_id, args->operation.length, command,
                   nds::serialize_storage_write, return_value);
}

extern "C" uint32_t nds_aicpu_storage_batch_read(const NdsStorageBatchOperationArgs *args, int32_t *return_value) {
    const uint32_t total = batch_bytes(args, false);
    const nds::StorageBatchReadCommand command{
        0U,
        args == nullptr ? 0U : args->operation.entry_count,
        total,
        {args == nullptr ? 0U : args->operation.entries_address,
         args == nullptr ? 0U : static_cast<uint64_t>(args->operation.entry_count) * nds::kStorageBatchEntryBytes,
         args == nullptr ? 0U : args->operation.entries_key},
        0U};
    return execute(args == nullptr ? nullptr : &args->storage, args == nullptr ? 0U : args->operation.slot_id, total,
                   command, nds::serialize_storage_batch_read, return_value);
}

extern "C" uint32_t nds_aicpu_storage_batch_write(const NdsStorageBatchOperationArgs *args, int32_t *return_value) {
    const uint32_t total = batch_bytes(args, true);
    const nds::StorageBatchWriteCommand command{
        0U,
        args == nullptr ? 0U : args->operation.entry_count,
        total,
        {args == nullptr ? 0U : args->operation.entries_address,
         args == nullptr ? 0U : static_cast<uint64_t>(args->operation.entry_count) * nds::kStorageBatchEntryBytes,
         args == nullptr ? 0U : args->operation.entries_key},
        0U};
    return execute(args == nullptr ? nullptr : &args->storage, args == nullptr ? 0U : args->operation.slot_id, total,
                   command, nds::serialize_storage_batch_write, return_value);
}

extern "C" uint32_t nds_aicpu_storage_wait(const NdsStorageDescriptor *descriptor, uint32_t slot_id,
                                           int32_t *return_value) {
    if (return_value == nullptr || !nds_storage_slot_id_valid(descriptor, slot_id))
        return kNdsAicpuInvalidArgument;
    const uint32_t slot_index = nds_storage_slot_id_index(slot_id);
    const NdsStorageState *state = nds_storage_state(descriptor, slot_index);
    const NdsStorageSlotDescriptor *slot = nds_storage_slot(descriptor, slot_index);
    if (state == nullptr || slot == nullptr || state->in_flight == 0U)
        return kNdsAicpuInvalidArgument;
    uint8_t observed[nds::kStorageCompletionBytes]{};
    auto *completion = reinterpret_cast<volatile uint8_t *>(slot->completion_buffer.address);
    for (uint32_t index = 0U; index < sizeof(observed); ++index) observed[index] = completion[index];
    nds::StorageCompletion decoded{};
    if (nds::deserialize_storage_completion(observed, sizeof(observed), &decoded) != nds::StorageSerdeResult::Ok ||
        decoded.state != nds::StorageCompletionState::Complete)
        return (NdsAicpuSetReturnValue(return_value, NDS_OPERATION_QUEUE_FULL), kNdsAicpuSuccess);
    if (decoded.command_id != state->command_id || decoded.bytes_transferred != state->expected_bytes)
        return (NdsAicpuSetReturnValue(return_value, NDS_OPERATION_INVALID_ARGUMENT), kNdsAicpuSuccess);
    NdsAicpuSetReturnValue(return_value, decoded.status == nds::StorageStatus::Success ? NDS_OPERATION_SUCCESS
                                                                                       : NDS_OPERATION_PROVIDER_FAILED);
    return kNdsAicpuSuccess;
}
