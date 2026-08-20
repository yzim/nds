#include "api.h"
#include "internal.h"
#include "nds/device_storage.h"

namespace {

template <typename Request, typename Serialize>
uint32_t execute(const NdsDeviceStorageContext *context, const Request *command, Serialize serialize,
                 NdsDeviceOperationResult *result) {
    if (result == nullptr)
        return kNdsAicpuInvalidArgument;
    uint8_t pending[nds::kStorageCompletionBytes]{};
    uint8_t command_bytes[nds::kStorageCommandBytes]{};
    const nds::StorageCompletion pending_completion{command->command_id, nds::StorageCompletionState::Pending,
                                                    nds::StorageStatus::Success, 0U};
    if (nds::serialize_storage_completion(pending_completion, pending, sizeof(pending)) !=
            nds::StorageSerdeResult::Ok ||
        serialize(*command, command_bytes, sizeof(command_bytes)) != nds::StorageSerdeResult::Ok) {
        NdsAicpuSetResult(result, NDS_DEVICE_OPERATION_INVALID_ARGUMENT, NDS_DEVICE_OPERATION_PATH_NONE, 0);
        return kNdsAicpuSuccess;
    }
    auto *completion_address = reinterpret_cast<uint8_t *>(context->completion.address);
    auto *command_address = reinterpret_cast<uint8_t *>(context->command_buffer.address);
    for (uint32_t index = 0U; index < sizeof(pending); ++index)
        completion_address[index] = pending[index];
    for (uint32_t index = 0U; index < sizeof(command_bytes); ++index)
        command_address[index] = command_bytes[index];
    NdsAicpuBarrier();
    const NdsDeviceTransfer transfer{command->command_id,
                                     {context->command_buffer.address, nds::kStorageCommandBytes,
                                      context->command_buffer.local_key},
                                     0U,
                                     0U,
                                     0U};
    const uint32_t sent = NdsAicpuRdmaSendImpl(&context->transport, &transfer, result);
    return sent;
}

uint32_t wait_for_completion(const NdsDeviceStorageContext *context, uint64_t command_id, uint64_t expected_bytes,
                             NdsDeviceOperationResult *result) {
    if (result == nullptr || !nds_device_storage_wait_valid(context, command_id, expected_bytes))
        return kNdsAicpuInvalidArgument;
    auto *completion = reinterpret_cast<volatile uint8_t *>(context->completion.address);
    for (;;) {
        uint8_t observed[nds::kStorageCompletionBytes]{};
        for (uint32_t index = 0U; index < sizeof(observed); ++index)
            observed[index] = completion[index];
        NdsAicpuBarrier();
        nds::StorageCompletion decoded{};
        if (nds::deserialize_storage_completion(observed, sizeof(observed), &decoded) != nds::StorageSerdeResult::Ok ||
            decoded.state != nds::StorageCompletionState::Complete)
            continue;
        if (decoded.command_id != command_id || decoded.bytes_transferred != expected_bytes) {
            NdsAicpuSetResult(result, NDS_DEVICE_OPERATION_INVALID_ARGUMENT, NDS_DEVICE_OPERATION_PATH_NONE, 0);
            return kNdsAicpuSuccess;
        }
        NdsAicpuSetResult(result, decoded.status == nds::StorageStatus::Success ? NDS_DEVICE_OPERATION_SUCCESS
                                                                                  : NDS_DEVICE_OPERATION_PROVIDER_FAILED,
                          NDS_DEVICE_OPERATION_PATH_DIRECT, 0);
        result->reserved = NDS_DEVICE_OPERATION_TERMINAL;
        NdsAicpuBarrier();
        return kNdsAicpuSuccess;
    }
}

}  // namespace

extern "C" uint32_t NdsAicpuStorageReadImpl(const NdsDeviceStorageContext *context,
                                             const nds::StorageReadCommand *command,
                                             NdsDeviceOperationResult *result) {
    if (!nds_device_storage_read_valid(context, command)) {
        if (result != nullptr)
            NdsAicpuSetResult(result, NDS_DEVICE_OPERATION_INVALID_ARGUMENT, NDS_DEVICE_OPERATION_PATH_NONE, 0);
        return result == nullptr ? kNdsAicpuInvalidArgument : kNdsAicpuSuccess;
    }
    return execute(context, command, nds::serialize_storage_read, result);
}

extern "C" uint32_t NdsAicpuStorageWriteImpl(const NdsDeviceStorageContext *context,
                                              const nds::StorageWriteCommand *command,
                                              NdsDeviceOperationResult *result) {
    if (!nds_device_storage_write_valid(context, command)) {
        if (result != nullptr)
            NdsAicpuSetResult(result, NDS_DEVICE_OPERATION_INVALID_ARGUMENT, NDS_DEVICE_OPERATION_PATH_NONE, 0);
        return result == nullptr ? kNdsAicpuInvalidArgument : kNdsAicpuSuccess;
    }
    return execute(context, command, nds::serialize_storage_write, result);
}

extern "C" uint32_t NdsAicpuStorageBatchReadImpl(const NdsDeviceStorageContext *context,
                                                  const nds::StorageBatchReadCommand *command,
                                                  NdsDeviceOperationResult *result) {
    if (!nds_device_storage_batch_read_valid(context, command)) {
        if (result != nullptr)
            NdsAicpuSetResult(result, NDS_DEVICE_OPERATION_INVALID_ARGUMENT, NDS_DEVICE_OPERATION_PATH_NONE, 0);
        return result == nullptr ? kNdsAicpuInvalidArgument : kNdsAicpuSuccess;
    }
    return execute(context, command, nds::serialize_storage_batch_read, result);
}

extern "C" uint32_t NdsAicpuStorageBatchWriteImpl(const NdsDeviceStorageContext *context,
                                                   const nds::StorageBatchWriteCommand *command,
                                                   NdsDeviceOperationResult *result) {
    if (!nds_device_storage_batch_write_valid(context, command)) {
        if (result != nullptr)
            NdsAicpuSetResult(result, NDS_DEVICE_OPERATION_INVALID_ARGUMENT, NDS_DEVICE_OPERATION_PATH_NONE, 0);
        return result == nullptr ? kNdsAicpuInvalidArgument : kNdsAicpuSuccess;
    }
    return execute(context, command, nds::serialize_storage_batch_write, result);
}

extern "C" uint32_t NdsAicpuStorageWaitImpl(const NdsDeviceStorageContext *context, uint64_t command_id,
                                              uint64_t expected_bytes, NdsDeviceOperationResult *result) {
    return wait_for_completion(context, command_id, expected_bytes, result);
}
