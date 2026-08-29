#include "api.h"
#include "internal.h"
#include "device_storage.h"

namespace {

template <typename Request, typename Serialize>
uint32_t execute(const NdsDeviceStorageContext *context, const Request *command, Serialize serialize,
                 int32_t *return_value) {
    if (return_value == nullptr)
        return kNdsAicpuInvalidArgument;
    uint8_t pending[nds::kStorageCompletionBytes]{};
    uint8_t command_bytes[nds::kStorageCommandBytes]{};
    const nds::StorageCompletion pending_completion{command->command_id, nds::StorageCompletionState::Pending,
                                                    nds::StorageStatus::Success, 0U};
    if (nds::serialize_storage_completion(pending_completion, pending, sizeof(pending)) !=
            nds::StorageSerdeResult::Ok ||
        serialize(*command, command_bytes, sizeof(command_bytes)) != nds::StorageSerdeResult::Ok) {
        NdsAicpuSetReturnValue(return_value, NDS_DEVICE_OPERATION_INVALID_ARGUMENT);
        return kNdsAicpuSuccess;
    }
    auto *completion_address = reinterpret_cast<uint8_t *>(context->completion.address);
    auto *command_address = reinterpret_cast<uint8_t *>(context->command_buffer.address);
    for (uint32_t index = 0U; index < sizeof(pending); ++index) completion_address[index] = pending[index];
    for (uint32_t index = 0U; index < sizeof(command_bytes); ++index) command_address[index] = command_bytes[index];
    NdsAicpuBarrier();
    const NdsDeviceSendWr transfer{
        command->command_id,
        NDS_DEVICE_WR_SEND,
        NDS_DEVICE_SEND_SIGNALED,
        {context->command_buffer.address, nds::kStorageCommandBytes, context->command_buffer.local_key},
        0U,
        0U,
        0U};
    const uint32_t sent = nds_aicpu_rdma_send(&context->transport, &transfer, return_value);
    return sent;
}

uint32_t wait_for_completion(const NdsDeviceStorageContext *context, uint64_t command_id, uint64_t expected_bytes,
                             int32_t *return_value) {
    if (return_value == nullptr || !nds_device_storage_wait_valid(context, command_id, expected_bytes))
        return kNdsAicpuInvalidArgument;
    auto *completion = reinterpret_cast<volatile uint8_t *>(context->completion.address);
    for (;;) {
        uint8_t observed[nds::kStorageCompletionBytes]{};
        for (uint32_t index = 0U; index < sizeof(observed); ++index) observed[index] = completion[index];
        NdsAicpuBarrier();
        nds::StorageCompletion decoded{};
        if (nds::deserialize_storage_completion(observed, sizeof(observed), &decoded) != nds::StorageSerdeResult::Ok ||
            decoded.state != nds::StorageCompletionState::Complete)
            continue;
        if (decoded.command_id != command_id || decoded.bytes_transferred != expected_bytes) {
            NdsAicpuSetReturnValue(return_value, NDS_DEVICE_OPERATION_INVALID_ARGUMENT);
            return kNdsAicpuSuccess;
        }
        NdsAicpuSetReturnValue(return_value, decoded.status == nds::StorageStatus::Success
                                                 ? NDS_DEVICE_OPERATION_SUCCESS
                                                 : NDS_DEVICE_OPERATION_PROVIDER_FAILED);
        return kNdsAicpuSuccess;
    }
}

}  // namespace

extern "C" uint32_t nds_aicpu_storage_read(const NdsDeviceStorageContext *context,
                                           const nds::StorageReadCommand *command, int32_t *return_value) {
    if (!nds_device_storage_read_valid(context, command)) {
        if (return_value != nullptr)
            NdsAicpuSetReturnValue(return_value, NDS_DEVICE_OPERATION_INVALID_ARGUMENT);
        return return_value == nullptr ? kNdsAicpuInvalidArgument : kNdsAicpuSuccess;
    }
    return execute(context, command, nds::serialize_storage_read, return_value);
}

extern "C" uint32_t nds_aicpu_storage_write(const NdsDeviceStorageContext *context,
                                            const nds::StorageWriteCommand *command, int32_t *return_value) {
    if (!nds_device_storage_write_valid(context, command)) {
        if (return_value != nullptr)
            NdsAicpuSetReturnValue(return_value, NDS_DEVICE_OPERATION_INVALID_ARGUMENT);
        return return_value == nullptr ? kNdsAicpuInvalidArgument : kNdsAicpuSuccess;
    }
    return execute(context, command, nds::serialize_storage_write, return_value);
}

extern "C" uint32_t nds_aicpu_storage_batch_read(const NdsDeviceStorageContext *context,
                                                 const nds::StorageBatchReadCommand *command, int32_t *return_value) {
    if (!nds_device_storage_batch_read_valid(context, command)) {
        if (return_value != nullptr)
            NdsAicpuSetReturnValue(return_value, NDS_DEVICE_OPERATION_INVALID_ARGUMENT);
        return return_value == nullptr ? kNdsAicpuInvalidArgument : kNdsAicpuSuccess;
    }
    return execute(context, command, nds::serialize_storage_batch_read, return_value);
}

extern "C" uint32_t nds_aicpu_storage_batch_write(const NdsDeviceStorageContext *context,
                                                  const nds::StorageBatchWriteCommand *command, int32_t *return_value) {
    if (!nds_device_storage_batch_write_valid(context, command)) {
        if (return_value != nullptr)
            NdsAicpuSetReturnValue(return_value, NDS_DEVICE_OPERATION_INVALID_ARGUMENT);
        return return_value == nullptr ? kNdsAicpuInvalidArgument : kNdsAicpuSuccess;
    }
    return execute(context, command, nds::serialize_storage_batch_write, return_value);
}

extern "C" uint32_t nds_aicpu_storage_wait(const NdsDeviceStorageContext *context, uint64_t command_id,
                                           uint64_t expected_bytes, int32_t *return_value) {
    return wait_for_completion(context, command_id, expected_bytes, return_value);
}
