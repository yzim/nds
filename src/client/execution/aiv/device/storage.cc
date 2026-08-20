#include "api.h"
#include "internal.h"
#include "nds/device_storage.h"

namespace {

__aicore__ inline void StoreBytes(__gm__ uint8_t *destination, const uint8_t *source, uint32_t length) {
    for (uint32_t index = 0U; index < length; ++index)
        destination[index] = source[index];
    NdsAivCacheSync(destination, length);
}

__aicore__ inline void LoadBytes(const __gm__ uint8_t *source, uint8_t *destination, uint32_t length) {
    for (uint32_t index = 0U; index < length; ++index)
        destination[index] = source[index];
}

__aicore__ inline bool ContextValid(__gm__ const NdsDeviceStorageContext *context) {
    return context != nullptr && context->abi_version == NDS_DEVICE_STORAGE_ABI_VERSION &&
           context->size == sizeof(NdsDeviceStorageContext) &&
           context->transport.abi_version == NDS_DEVICE_TRANSPORT_ABI_VERSION &&
           context->transport.size == sizeof(NdsDeviceTransport) && context->command_buffer.address != 0U &&
           context->command_buffer.local_key != 0U && context->command_buffer.length >= nds::kStorageCommandBytes &&
           context->completion.address != 0U && context->completion.local_key != 0U &&
           context->completion.length >= nds::kStorageCompletionBytes && context->capacity != 0U;
}

__aicore__ inline bool SingleCommandValid(__gm__ const NdsDeviceStorageContext *context, uint64_t command_id,
                                          uint64_t offset, uint64_t length, const nds::StorageMemory &data) {
    return ContextValid(context) && command_id != 0U && length != 0U && data.address != 0U &&
           data.remote_key != 0U && data.length >= length && offset <= context->capacity &&
           length <= context->capacity - offset;
}

__aicore__ inline bool BatchCommandValid(__gm__ const NdsDeviceStorageContext *context, uint64_t command_id,
                                         uint64_t entry_count, uint64_t total_length,
                                         const nds::StorageMemory &entries) {
    return ContextValid(context) && command_id != 0U && entry_count != 0U &&
           entry_count <= nds::kStorageMaxBatchEntries && total_length != 0U && entries.address != 0U &&
           entries.remote_key != 0U && entries.length >= entry_count * nds::kStorageBatchEntryBytes;
}

__aicore__ inline void ExecuteSerialized(__gm__ const NdsDeviceStorageContext *context, uint64_t command_id,
                                         const uint8_t *command_bytes, TBuf<> *scratch,
                                         __gm__ NdsDeviceOperationResult *result) {
    uint8_t pending[nds::kStorageCompletionBytes]{};
    const nds::StorageCompletion pending_completion{command_id, nds::StorageCompletionState::Pending,
                                                    nds::StorageStatus::Success, 0U};
    if (nds::serialize_storage_completion(pending_completion, pending, sizeof(pending)) !=
        nds::StorageSerdeResult::Ok) {
        NdsAivSetResult(result, NDS_DEVICE_OPERATION_INVALID_ARGUMENT);
        return;
    }
    StoreBytes(reinterpret_cast<__gm__ uint8_t *>(context->completion.address), pending, sizeof(pending));
    StoreBytes(reinterpret_cast<__gm__ uint8_t *>(context->command_buffer.address), command_bytes,
               nds::kStorageCommandBytes);
    const NdsDeviceTransfer transfer{command_id,
                                     {context->command_buffer.address, nds::kStorageCommandBytes,
                                      context->command_buffer.local_key},
                                     0U, 0U, 0U};
    NdsAivRdmaSendImpl(&context->transport, &transfer, scratch, result);
}

__aicore__ inline void WaitForCompletion(__gm__ const NdsDeviceStorageContext *context, uint64_t command_id,
                                         uint64_t expected_bytes, __gm__ NdsDeviceOperationResult *result) {
    if (!ContextValid(context) || command_id == 0U || expected_bytes == 0U) {
        NdsAivSetResult(result, NDS_DEVICE_OPERATION_INVALID_ARGUMENT);
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
            NdsAivSetResult(result, NDS_DEVICE_OPERATION_INVALID_ARGUMENT);
            return;
        }
        NdsAivSetResult(result, decoded.status == nds::StorageStatus::Success ? NDS_DEVICE_OPERATION_SUCCESS
                                                                                : NDS_DEVICE_OPERATION_PROVIDER_FAILED);
        if (result != nullptr) {
            result->reserved = NDS_DEVICE_OPERATION_TERMINAL;
            NdsAivCacheSync(reinterpret_cast<__gm__ uint8_t *>(result), sizeof(*result));
        }
        return;
    }
}

}  // namespace

NDS_AIV_DEVICE_API_LINKAGE __aicore__ void NdsAivStorageReadImpl(
    __gm__ const NdsDeviceStorageContext *context, __gm__ const nds::StorageReadCommand *command, TBuf<> *scratch,
    __gm__ NdsDeviceOperationResult *result) {
    if (result == nullptr)
        return;
    if (command == nullptr) {
        NdsAivSetResult(result, NDS_DEVICE_OPERATION_INVALID_ARGUMENT);
        return;
    }
    const nds::StorageReadCommand local{command->command_id, command->offset, command->length,
                                        {command->data.address, command->data.length, command->data.remote_key}};
    if (!SingleCommandValid(context, local.command_id, local.offset, local.length, local.data)) {
        NdsAivSetResult(result, NDS_DEVICE_OPERATION_INVALID_ARGUMENT);
        return;
    }
    uint8_t command_bytes[nds::kStorageCommandBytes]{};
    if (nds::serialize_storage_read(local, command_bytes, sizeof(command_bytes)) != nds::StorageSerdeResult::Ok) {
        NdsAivSetResult(result, NDS_DEVICE_OPERATION_INVALID_ARGUMENT);
        return;
    }
    ExecuteSerialized(context, local.command_id, command_bytes, scratch, result);
}

NDS_AIV_DEVICE_API_LINKAGE __aicore__ void NdsAivStorageWriteImpl(
    __gm__ const NdsDeviceStorageContext *context, __gm__ const nds::StorageWriteCommand *command, TBuf<> *scratch,
    __gm__ NdsDeviceOperationResult *result) {
    if (result == nullptr)
        return;
    if (command == nullptr) {
        NdsAivSetResult(result, NDS_DEVICE_OPERATION_INVALID_ARGUMENT);
        return;
    }
    const nds::StorageWriteCommand local{command->command_id, command->offset, command->length,
                                         {command->data.address, command->data.length, command->data.remote_key}};
    if (!SingleCommandValid(context, local.command_id, local.offset, local.length, local.data)) {
        NdsAivSetResult(result, NDS_DEVICE_OPERATION_INVALID_ARGUMENT);
        return;
    }
    uint8_t command_bytes[nds::kStorageCommandBytes]{};
    if (nds::serialize_storage_write(local, command_bytes, sizeof(command_bytes)) != nds::StorageSerdeResult::Ok) {
        NdsAivSetResult(result, NDS_DEVICE_OPERATION_INVALID_ARGUMENT);
        return;
    }
    ExecuteSerialized(context, local.command_id, command_bytes, scratch, result);
}

NDS_AIV_DEVICE_API_LINKAGE __aicore__ void NdsAivStorageBatchReadImpl(
    __gm__ const NdsDeviceStorageContext *context, __gm__ const nds::StorageBatchReadCommand *command,
    TBuf<> *scratch, __gm__ NdsDeviceOperationResult *result) {
    if (result == nullptr)
        return;
    if (command == nullptr) {
        NdsAivSetResult(result, NDS_DEVICE_OPERATION_INVALID_ARGUMENT);
        return;
    }
    const nds::StorageBatchReadCommand local{
        command->command_id, command->entry_count, command->total_length,
        {command->entries.address, command->entries.length, command->entries.remote_key}};
    if (!BatchCommandValid(context, local.command_id, local.entry_count, local.total_length, local.entries)) {
        NdsAivSetResult(result, NDS_DEVICE_OPERATION_INVALID_ARGUMENT);
        return;
    }
    uint8_t command_bytes[nds::kStorageCommandBytes]{};
    if (nds::serialize_storage_batch_read(local, command_bytes, sizeof(command_bytes)) !=
        nds::StorageSerdeResult::Ok) {
        NdsAivSetResult(result, NDS_DEVICE_OPERATION_INVALID_ARGUMENT);
        return;
    }
    ExecuteSerialized(context, local.command_id, command_bytes, scratch, result);
}

NDS_AIV_DEVICE_API_LINKAGE __aicore__ void NdsAivStorageBatchWriteImpl(
    __gm__ const NdsDeviceStorageContext *context, __gm__ const nds::StorageBatchWriteCommand *command,
    TBuf<> *scratch, __gm__ NdsDeviceOperationResult *result) {
    if (result == nullptr)
        return;
    if (command == nullptr) {
        NdsAivSetResult(result, NDS_DEVICE_OPERATION_INVALID_ARGUMENT);
        return;
    }
    const nds::StorageBatchWriteCommand local{
        command->command_id, command->entry_count, command->total_length,
        {command->entries.address, command->entries.length, command->entries.remote_key}};
    if (!BatchCommandValid(context, local.command_id, local.entry_count, local.total_length, local.entries)) {
        NdsAivSetResult(result, NDS_DEVICE_OPERATION_INVALID_ARGUMENT);
        return;
    }
    uint8_t command_bytes[nds::kStorageCommandBytes]{};
    if (nds::serialize_storage_batch_write(local, command_bytes, sizeof(command_bytes)) !=
        nds::StorageSerdeResult::Ok) {
        NdsAivSetResult(result, NDS_DEVICE_OPERATION_INVALID_ARGUMENT);
        return;
    }
    ExecuteSerialized(context, local.command_id, command_bytes, scratch, result);
}

NDS_AIV_DEVICE_API_LINKAGE __aicore__ void NdsAivStorageWaitImpl(__gm__ const NdsDeviceStorageContext *context,
                                                                 uint64_t command_id, uint64_t expected_bytes,
                                                                 __gm__ NdsDeviceOperationResult *result) {
    WaitForCompletion(context, command_id, expected_bytes, result);
}
