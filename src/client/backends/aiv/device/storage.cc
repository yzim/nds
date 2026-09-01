#include "api.h"
#include "internal.h"
#include "backend_storage.h"

namespace {

__aicore__ inline void StoreBytes(__gm__ uint8_t *destination, const uint8_t *source, uint32_t length) {
    for (uint32_t index = 0U; index < length; ++index) destination[index] = source[index];
    NdsAivCacheSync(destination, length);
}

__aicore__ inline void LoadBytes(const __gm__ uint8_t *source, uint8_t *destination, uint32_t length) {
    for (uint32_t index = 0U; index < length; ++index) destination[index] = source[index];
}

__aicore__ inline bool ContextValid(__gm__ const NdsStorageContext *context) {
    return context != nullptr && context->command_buffer.address != 0U && context->command_buffer.local_key != 0U &&
           context->command_buffer.length >= nds::kStorageCommandBytes && context->completion.address != 0U &&
           context->completion.local_key != 0U && context->completion.length >= nds::kStorageCompletionBytes &&
           context->capacity != 0U;
}

__aicore__ inline bool SingleCommandValid(__gm__ const NdsStorageContext *context, uint64_t command_id, uint64_t offset,
                                          uint64_t length, const nds::StorageMemory &data) {
    return ContextValid(context) && command_id != 0U && length != 0U && data.address != 0U && data.remote_key != 0U &&
           data.length >= length && offset <= context->capacity && length <= context->capacity - offset;
}

__aicore__ inline bool BatchCommandValid(__gm__ const NdsStorageContext *context, uint64_t command_id,
                                         uint64_t entry_count, uint64_t total_length,
                                         const nds::StorageMemory &entries) {
    return ContextValid(context) && command_id != 0U && entry_count != 0U &&
           entry_count <= nds::kStorageMaxBatchEntries && total_length != 0U && entries.address != 0U &&
           entries.remote_key != 0U && entries.length >= entry_count * nds::kStorageBatchEntryBytes;
}

__aicore__ inline void ExecuteSerialized(__gm__ const NdsStorageContext *context, uint64_t command_id,
                                         const uint8_t *command_bytes, __gm__ int32_t *return_value, TBuf<> *scratch) {
    uint8_t pending[nds::kStorageCompletionBytes]{};
    const nds::StorageCompletion pending_completion{command_id, nds::StorageCompletionState::Pending,
                                                    nds::StorageStatus::Success, 0U};
    if (nds::serialize_storage_completion(pending_completion, pending, sizeof(pending)) !=
        nds::StorageSerdeResult::Ok) {
        NdsAivSetReturnValue(return_value, NDS_OPERATION_INVALID_ARGUMENT);
        return;
    }
    StoreBytes(reinterpret_cast<__gm__ uint8_t *>(context->completion.address), pending, sizeof(pending));
    StoreBytes(reinterpret_cast<__gm__ uint8_t *>(context->command_buffer.address), command_bytes,
               nds::kStorageCommandBytes);
    const NdsSendWr transfer{
        command_id, NDS_WR_SEND,
        0U,         {context->command_buffer.address, nds::kStorageCommandBytes, context->command_buffer.local_key},
        0U,         0U,
        0U};
    nds_aiv_rdma_send(&context->transport, 0U, &transfer, return_value, scratch);
}

__aicore__ inline void WaitForCompletion(__gm__ const NdsStorageContext *context, uint64_t command_id,
                                         uint64_t expected_bytes, __gm__ int32_t *return_value) {
    if (!ContextValid(context) || command_id == 0U || expected_bytes == 0U) {
        NdsAivSetReturnValue(return_value, NDS_OPERATION_INVALID_ARGUMENT);
        return;
    }
    __gm__ const uint8_t *completion = reinterpret_cast<__gm__ const uint8_t *>(context->completion.address);
    for (;;) {
        uint8_t observed[nds::kStorageCompletionBytes]{};
        NdsAivCacheSync(reinterpret_cast<__gm__ uint8_t *>(context->completion.address), sizeof(observed));
        LoadBytes(completion, observed, sizeof(observed));
        nds::StorageCompletion decoded{};
        if (nds::deserialize_storage_completion(observed, sizeof(observed), &decoded) != nds::StorageSerdeResult::Ok ||
            decoded.state != nds::StorageCompletionState::Complete)
            continue;
        if (decoded.command_id != command_id || decoded.bytes_transferred != expected_bytes) {
            NdsAivSetReturnValue(return_value, NDS_OPERATION_INVALID_ARGUMENT);
            return;
        }
        NdsAivSetReturnValue(return_value, decoded.status == nds::StorageStatus::Success
                                               ? NDS_OPERATION_SUCCESS
                                               : NDS_OPERATION_PROVIDER_FAILED);
        return;
    }
}

}  // namespace

NDS_AIV_DEVICE_API_LINKAGE __aicore__ void nds_aiv_storage_read(__gm__ const NdsStorageContext *context,
                                                                __gm__ const nds::StorageReadCommand *command,
                                                                __gm__ int32_t *return_value, TBuf<> *scratch) {
    if (return_value == nullptr)
        return;
    if (command == nullptr) {
        NdsAivSetReturnValue(return_value, NDS_OPERATION_INVALID_ARGUMENT);
        return;
    }
    const nds::StorageReadCommand local{command->command_id,
                                        command->offset,
                                        command->length,
                                        {command->data.address, command->data.length, command->data.remote_key}};
    if (!SingleCommandValid(context, local.command_id, local.offset, local.length, local.data)) {
        NdsAivSetReturnValue(return_value, NDS_OPERATION_INVALID_ARGUMENT);
        return;
    }
    uint8_t command_bytes[nds::kStorageCommandBytes]{};
    if (nds::serialize_storage_read(local, command_bytes, sizeof(command_bytes)) != nds::StorageSerdeResult::Ok) {
        NdsAivSetReturnValue(return_value, NDS_OPERATION_INVALID_ARGUMENT);
        return;
    }
    ExecuteSerialized(context, local.command_id, command_bytes, return_value, scratch);
}

NDS_AIV_DEVICE_API_LINKAGE __aicore__ void nds_aiv_storage_write(__gm__ const NdsStorageContext *context,
                                                                 __gm__ const nds::StorageWriteCommand *command,
                                                                 __gm__ int32_t *return_value, TBuf<> *scratch) {
    if (return_value == nullptr)
        return;
    if (command == nullptr) {
        NdsAivSetReturnValue(return_value, NDS_OPERATION_INVALID_ARGUMENT);
        return;
    }
    const nds::StorageWriteCommand local{command->command_id,
                                         command->offset,
                                         command->length,
                                         {command->data.address, command->data.length, command->data.remote_key}};
    if (!SingleCommandValid(context, local.command_id, local.offset, local.length, local.data)) {
        NdsAivSetReturnValue(return_value, NDS_OPERATION_INVALID_ARGUMENT);
        return;
    }
    uint8_t command_bytes[nds::kStorageCommandBytes]{};
    if (nds::serialize_storage_write(local, command_bytes, sizeof(command_bytes)) != nds::StorageSerdeResult::Ok) {
        NdsAivSetReturnValue(return_value, NDS_OPERATION_INVALID_ARGUMENT);
        return;
    }
    ExecuteSerialized(context, local.command_id, command_bytes, return_value, scratch);
}

NDS_AIV_DEVICE_API_LINKAGE __aicore__ void nds_aiv_storage_batch_read(
    __gm__ const NdsStorageContext *context, __gm__ const nds::StorageBatchReadCommand *command,
    __gm__ int32_t *return_value, TBuf<> *scratch) {
    if (return_value == nullptr)
        return;
    if (command == nullptr) {
        NdsAivSetReturnValue(return_value, NDS_OPERATION_INVALID_ARGUMENT);
        return;
    }
    const nds::StorageBatchReadCommand local{
        command->command_id,
        command->entry_count,
        command->total_length,
        {command->entries.address, command->entries.length, command->entries.remote_key}};
    if (!BatchCommandValid(context, local.command_id, local.entry_count, local.total_length, local.entries)) {
        NdsAivSetReturnValue(return_value, NDS_OPERATION_INVALID_ARGUMENT);
        return;
    }
    uint8_t command_bytes[nds::kStorageCommandBytes]{};
    if (nds::serialize_storage_batch_read(local, command_bytes, sizeof(command_bytes)) != nds::StorageSerdeResult::Ok) {
        NdsAivSetReturnValue(return_value, NDS_OPERATION_INVALID_ARGUMENT);
        return;
    }
    ExecuteSerialized(context, local.command_id, command_bytes, return_value, scratch);
}

NDS_AIV_DEVICE_API_LINKAGE __aicore__ void nds_aiv_storage_batch_write(
    __gm__ const NdsStorageContext *context, __gm__ const nds::StorageBatchWriteCommand *command,
    __gm__ int32_t *return_value, TBuf<> *scratch) {
    if (return_value == nullptr)
        return;
    if (command == nullptr) {
        NdsAivSetReturnValue(return_value, NDS_OPERATION_INVALID_ARGUMENT);
        return;
    }
    const nds::StorageBatchWriteCommand local{
        command->command_id,
        command->entry_count,
        command->total_length,
        {command->entries.address, command->entries.length, command->entries.remote_key}};
    if (!BatchCommandValid(context, local.command_id, local.entry_count, local.total_length, local.entries)) {
        NdsAivSetReturnValue(return_value, NDS_OPERATION_INVALID_ARGUMENT);
        return;
    }
    uint8_t command_bytes[nds::kStorageCommandBytes]{};
    if (nds::serialize_storage_batch_write(local, command_bytes, sizeof(command_bytes)) !=
        nds::StorageSerdeResult::Ok) {
        NdsAivSetReturnValue(return_value, NDS_OPERATION_INVALID_ARGUMENT);
        return;
    }
    ExecuteSerialized(context, local.command_id, command_bytes, return_value, scratch);
}

NDS_AIV_DEVICE_API_LINKAGE __aicore__ void nds_aiv_storage_wait(__gm__ const NdsStorageContext *context,
                                                                uint64_t command_id, uint64_t expected_bytes,
                                                                __gm__ int32_t *return_value) {
    WaitForCompletion(context, command_id, expected_bytes, return_value);
}
