#include "api.h"
#include "internal.h"
#include "backend_storage.h"

namespace {

__aicore__ inline void StoreBytes(__gm__ uint8_t *destination, const uint8_t *source, uint32_t length) {
    for (uint32_t index = 0U; index < length; ++index) destination[index] = source[index];
    NdsAivCacheSync(destination, length);
}

__aicore__ inline bool ContextValid(__gm__ const NdsStorageDescriptor *context) {
    return context != nullptr && context->slot_descriptors_address != 0U && context->storage_states_address != 0U &&
           context->slot_count != 0U && context->transport.qp_count != 0U && context->capacity != 0U &&
           context->reserved == 0U && context->transport.reserved == 0U &&
           context->transport.qp_descriptors_address != 0U && context->transport.qp_states_address != 0U;
}

__aicore__ inline bool SlotIdValid(__gm__ const NdsStorageDescriptor *context, uint32_t slot_id) {
    const uint32_t slot_index = slot_id & UINT32_C(0xffff);
    const uint32_t queue_index = slot_id >> 16U;
    const __gm__ NdsStorageSlotDescriptor *slot = nds_storage_slot_global(context, slot_index);
    return ContextValid(context) && slot != nullptr && slot->reserved == 0U && slot->qp_index == queue_index &&
           slot->qp_index < context->transport.qp_count && slot->command_buffer.address != 0U &&
           slot->command_buffer.local_key != 0U && slot->command_buffer.length >= nds::kStorageCommandBytes &&
           slot->completion_buffer.address != 0U && slot->completion_buffer.local_key != 0U &&
           slot->completion_buffer.length >= nds::kStorageCompletionBytes;
}

__aicore__ inline bool Claim(__gm__ const NdsStorageDescriptor *context, uint32_t slot_id, uint32_t expected_bytes,
                             __gm__ NdsStorageState **state, __gm__ const NdsStorageSlotDescriptor **slot) {
    const uint32_t slot_index = slot_id & UINT32_C(0xffff);
    *state = nds_storage_state_global(context, slot_index);
    *slot = nds_storage_slot_global(context, slot_index);
    if (!ContextValid(context) || *state == nullptr || *slot == nullptr || !SlotIdValid(context, slot_id) ||
        (*state)->command_id == 0U || expected_bytes == 0U || (*state)->in_flight != 0U)
        return false;
    (*state)->expected_bytes = expected_bytes;
    (*state)->in_flight = 1U;
    (*state)->status = 0;
    return true;
}

__aicore__ inline void ExecuteSerialized(__gm__ const NdsStorageDescriptor *context, uint32_t slot_id,
                                         uint32_t expected_bytes, const uint8_t *command_bytes,
                                         __gm__ int32_t *return_value, TBuf<> *scratch) {
    __gm__ NdsStorageState *state = nullptr;
    __gm__ const NdsStorageSlotDescriptor *slot = nullptr;
    if (!Claim(context, slot_id, expected_bytes, &state, &slot)) {
        NdsAivSetReturnValue(return_value, NDS_OPERATION_QUEUE_FULL);
        return;
    }
    uint8_t pending[nds::kStorageCompletionBytes]{};
    const nds::StorageCompletion pending_completion{state->command_id, nds::StorageCompletionState::Pending,
                                                    nds::StorageStatus::Success, 0U};
    if (nds::serialize_storage_completion(pending_completion, pending, sizeof(pending)) !=
        nds::StorageSerdeResult::Ok) {
        state->in_flight = 0U;
        NdsAivSetReturnValue(return_value, NDS_OPERATION_INVALID_ARGUMENT);
        return;
    }
    StoreBytes(reinterpret_cast<__gm__ uint8_t *>(slot->completion_buffer.address), pending, sizeof(pending));
    StoreBytes(reinterpret_cast<__gm__ uint8_t *>(slot->command_buffer.address), command_bytes,
               nds::kStorageCommandBytes);
    const NdsSendWr transfer{
        .wr_id = state->command_id,
        .opcode = NDS_WR_SEND,
        .flags = 0U,
        .local = {.address = slot->command_buffer.address,
                  .length = nds::kStorageCommandBytes,
                  .local_key = slot->command_buffer.local_key},
        .remote_address = 0U,
        .remote_key = 0U,
    };
    nds_aiv_rdma_send(&context->transport, slot->qp_index, &transfer, return_value, scratch);
    state->status = *return_value;
}

__aicore__ inline uint32_t BatchTotal(__gm__ const NdsStorageBatchOperation *args, bool write) {
    if (args == nullptr || args->entries_address == 0U || args->entries_key == 0U || args->entry_count == 0U ||
        args->entry_count > nds::kStorageMaxBatchEntries)
        return 0U;
    uint64_t total = 0U;
    for (uint32_t index = 0U; index < args->entry_count; ++index) {
        __gm__ const uint8_t *bytes =
            reinterpret_cast<__gm__ const uint8_t *>(args->entries_address) + index * nds::kStorageBatchEntryBytes;
        uint8_t encoded[nds::kStorageBatchEntryBytes]{};
        for (uint32_t byte = 0U; byte < nds::kStorageBatchEntryBytes; ++byte) encoded[byte] = bytes[byte];
        uint64_t length = 0U;
        if (write) {
            nds::StorageBatchWriteEntry entry{};
            if (nds::deserialize_storage_batch_write_entry(encoded, nds::kStorageBatchEntryBytes, &entry) !=
                nds::StorageSerdeResult::Ok)
                return 0U;
            length = entry.length;
        } else {
            nds::StorageBatchReadEntry entry{};
            if (nds::deserialize_storage_batch_read_entry(encoded, nds::kStorageBatchEntryBytes, &entry) !=
                nds::StorageSerdeResult::Ok)
                return 0U;
            length = entry.length;
        }
        if (length == 0U || total > UINT32_MAX - length)
            return 0U;
        total += length;
    }
    return static_cast<uint32_t>(total);
}

}  // namespace

NDS_AIV_DEVICE_API_LINKAGE __aicore__ void nds_aiv_storage_bootstrap(
    __gm__ const NdsStorageBootstrapDescriptor *bootstrap, __gm__ int32_t *return_value, TBuf<> *scratch) {
    if (bootstrap == nullptr || bootstrap->bootstrap.address == 0U || bootstrap->bootstrap.local_key == 0U) {
        NdsAivSetReturnValue(return_value, NDS_OPERATION_INVALID_ARGUMENT);
        return;
    }
    const NdsSendWr transfer{.wr_id = 1U,
                             .opcode = NDS_WR_SEND,
                             .flags = 0U,
                             .local = {.address = bootstrap->bootstrap.address,
                                       .length = bootstrap->bootstrap.length,
                                       .local_key = bootstrap->bootstrap.local_key},
                             .remote_address = 0U,
                             .remote_key = 0U};
    nds_aiv_rdma_send(&bootstrap->transport, 0U, &transfer, return_value, scratch);
}

NDS_AIV_DEVICE_API_LINKAGE __aicore__ void nds_aiv_storage_read(__gm__ const NdsStorageDescriptor *storage,
                                                                __gm__ const NdsStorageOperation *operation,
                                                                __gm__ int32_t *return_value, TBuf<> *scratch) {
    if (storage == nullptr || operation == nullptr || operation->length == 0U) {
        NdsAivSetReturnValue(return_value, NDS_OPERATION_INVALID_ARGUMENT);
        return;
    }
    const uint32_t slot_index = operation->slot_id & UINT32_C(0xffff);
    __gm__ const NdsStorageState *state = nds_storage_state_global(storage, slot_index);
    const nds::StorageReadCommand command{state == nullptr ? 0U : state->command_id,
                                          operation->server_offset,
                                          operation->length,
                                          {operation->buffer_address, operation->length, operation->buffer_key},
                                          slot_index};
    uint8_t bytes[nds::kStorageCommandBytes]{};
    if (nds::serialize_storage_read(command, bytes, sizeof(bytes)) != nds::StorageSerdeResult::Ok) {
        NdsAivSetReturnValue(return_value, NDS_OPERATION_INVALID_ARGUMENT);
        return;
    }
    ExecuteSerialized(storage, operation->slot_id, operation->length, bytes, return_value, scratch);
}

NDS_AIV_DEVICE_API_LINKAGE __aicore__ void nds_aiv_storage_write(__gm__ const NdsStorageDescriptor *storage,
                                                                 __gm__ const NdsStorageOperation *operation,
                                                                 __gm__ int32_t *return_value, TBuf<> *scratch) {
    if (storage == nullptr || operation == nullptr || operation->length == 0U) {
        NdsAivSetReturnValue(return_value, NDS_OPERATION_INVALID_ARGUMENT);
        return;
    }
    const uint32_t slot_index = operation->slot_id & UINT32_C(0xffff);
    __gm__ const NdsStorageState *state = nds_storage_state_global(storage, slot_index);
    const nds::StorageWriteCommand command{state == nullptr ? 0U : state->command_id,
                                           operation->server_offset,
                                           operation->length,
                                           {operation->buffer_address, operation->length, operation->buffer_key},
                                           slot_index};
    uint8_t bytes[nds::kStorageCommandBytes]{};
    if (nds::serialize_storage_write(command, bytes, sizeof(bytes)) != nds::StorageSerdeResult::Ok) {
        NdsAivSetReturnValue(return_value, NDS_OPERATION_INVALID_ARGUMENT);
        return;
    }
    ExecuteSerialized(storage, operation->slot_id, operation->length, bytes, return_value, scratch);
}

NDS_AIV_DEVICE_API_LINKAGE __aicore__ void nds_aiv_storage_batch_read(__gm__ const NdsStorageDescriptor *storage,
                                                                      __gm__ const NdsStorageBatchOperation *operation,
                                                                      __gm__ int32_t *return_value, TBuf<> *scratch) {
    if (storage == nullptr || operation == nullptr || operation->entry_count == 0U ||
        operation->entry_count > nds::kStorageMaxBatchEntries) {
        NdsAivSetReturnValue(return_value, NDS_OPERATION_INVALID_ARGUMENT);
        return;
    }
    const uint32_t slot_index = operation->slot_id & UINT32_C(0xffff);
    __gm__ const NdsStorageState *state = nds_storage_state_global(storage, slot_index);
    const uint64_t entry_bytes = static_cast<uint64_t>(operation->entry_count) * nds::kStorageBatchEntryBytes;
    const uint32_t total = BatchTotal(operation, false);
    const nds::StorageBatchReadCommand command{state == nullptr ? 0U : state->command_id,
                                               operation->entry_count,
                                               total,
                                               {operation->entries_address, entry_bytes, operation->entries_key},
                                               slot_index};
    uint8_t bytes[nds::kStorageCommandBytes]{};
    if (nds::serialize_storage_batch_read(command, bytes, sizeof(bytes)) != nds::StorageSerdeResult::Ok) {
        NdsAivSetReturnValue(return_value, NDS_OPERATION_INVALID_ARGUMENT);
        return;
    }
    ExecuteSerialized(storage, operation->slot_id, total, bytes, return_value, scratch);
}

NDS_AIV_DEVICE_API_LINKAGE __aicore__ void nds_aiv_storage_batch_write(__gm__ const NdsStorageDescriptor *storage,
                                                                       __gm__ const NdsStorageBatchOperation *operation,
                                                                       __gm__ int32_t *return_value, TBuf<> *scratch) {
    if (storage == nullptr || operation == nullptr || operation->entry_count == 0U ||
        operation->entry_count > nds::kStorageMaxBatchEntries) {
        NdsAivSetReturnValue(return_value, NDS_OPERATION_INVALID_ARGUMENT);
        return;
    }
    const uint32_t slot_index = operation->slot_id & UINT32_C(0xffff);
    __gm__ const NdsStorageState *state = nds_storage_state_global(storage, slot_index);
    const uint64_t entry_bytes = static_cast<uint64_t>(operation->entry_count) * nds::kStorageBatchEntryBytes;
    const uint32_t total = BatchTotal(operation, true);
    const nds::StorageBatchWriteCommand command{state == nullptr ? 0U : state->command_id,
                                                operation->entry_count,
                                                total,
                                                {operation->entries_address, entry_bytes, operation->entries_key},
                                                slot_index};
    uint8_t bytes[nds::kStorageCommandBytes]{};
    if (nds::serialize_storage_batch_write(command, bytes, sizeof(bytes)) != nds::StorageSerdeResult::Ok) {
        NdsAivSetReturnValue(return_value, NDS_OPERATION_INVALID_ARGUMENT);
        return;
    }
    ExecuteSerialized(storage, operation->slot_id, total, bytes, return_value, scratch);
}

NDS_AIV_DEVICE_API_LINKAGE __aicore__ void nds_aiv_storage_wait(__gm__ const NdsStorageDescriptor *context,
                                                                uint32_t slot_id, __gm__ int32_t *return_value) {
    const uint32_t slot_index = slot_id & UINT32_C(0xffff);
    __gm__ const NdsStorageState *state = nds_storage_state_global(context, slot_index);
    const __gm__ NdsStorageSlotDescriptor *slot = nds_storage_slot_global(context, slot_index);
    if (!ContextValid(context) || state == nullptr || slot == nullptr || !SlotIdValid(context, slot_id) ||
        state->command_id == 0U || state->expected_bytes == 0U || state->in_flight == 0U) {
        NdsAivSetReturnValue(return_value, NDS_OPERATION_INVALID_ARGUMENT);
        return;
    }
    if (state->status != 0) {
        NdsAivSetReturnValue(return_value, static_cast<uint32_t>(-state->status));
        return;
    }
    uint8_t observed[nds::kStorageCompletionBytes]{};
    NdsAivCacheSync(reinterpret_cast<__gm__ uint8_t *>(slot->completion_buffer.address), sizeof(observed));
    for (uint32_t index = 0U; index < sizeof(observed); ++index)
        observed[index] = reinterpret_cast<__gm__ const uint8_t *>(slot->completion_buffer.address)[index];
    nds::StorageCompletion completion{};
    if (nds::deserialize_storage_completion(observed, sizeof(observed), &completion) != nds::StorageSerdeResult::Ok ||
        completion.state != nds::StorageCompletionState::Complete) {
        NdsAivSetReturnValue(return_value, NDS_OPERATION_QUEUE_FULL);
        return;
    }
    if (completion.command_id != state->command_id || completion.bytes_transferred != state->expected_bytes) {
        NdsAivSetReturnValue(return_value, NDS_OPERATION_INVALID_ARGUMENT);
        return;
    }
    NdsAivSetReturnValue(return_value, completion.status == nds::StorageStatus::Success
                                           ? NDS_OPERATION_SUCCESS
                                           : NDS_OPERATION_PROVIDER_FAILED);
}
