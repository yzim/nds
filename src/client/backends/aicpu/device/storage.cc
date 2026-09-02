#include "api.h"
#include "internal.h"
#include "backend_storage.h"

namespace {

template <typename Request, typename Serialize>
uint32_t execute(const NdsStorageDescriptor *descriptor, const Request *command, Serialize serialize,
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
        NdsAicpuSetReturnValue(return_value, NDS_OPERATION_INVALID_ARGUMENT);
        return kNdsAicpuSuccess;
    }
    const NdsStorageSlotDescriptor *slot = nds_storage_slot(descriptor, command->slot_index);
    if (slot == nullptr || !nds_storage_slot_valid(descriptor, command->slot_index)) {
        NdsAicpuSetReturnValue(return_value, NDS_OPERATION_INVALID_ARGUMENT);
        return kNdsAicpuSuccess;
    }
    auto *completion_address = reinterpret_cast<uint8_t *>(slot->completion_buffer.address);
    auto *command_address = reinterpret_cast<uint8_t *>(slot->command_buffer.address);
    for (uint32_t index = 0U; index < sizeof(pending); ++index) completion_address[index] = pending[index];
    for (uint32_t index = 0U; index < sizeof(command_bytes); ++index) command_address[index] = command_bytes[index];
    NdsAicpuBarrier();
    const NdsSendWr transfer{
        .wr_id = command->command_id,
        .opcode = NDS_WR_SEND,
        .flags = 0U,
        .local = {.address = slot->command_buffer.address,
                  .length = nds::kStorageCommandBytes,
                  .local_key = slot->command_buffer.local_key},
        .remote_address = 0U,
        .remote_key = 0U,
    };
    const uint32_t sent = nds_aicpu_rdma_send(&descriptor->transport, slot->qp_index, &transfer, return_value);
    return sent;
}

uint32_t wait_for_completion(const NdsStorageDescriptor *descriptor, uint64_t command_id, uint64_t expected_bytes,
                             uint32_t slot_index, int32_t *return_value) {
    if (return_value == nullptr || !nds_storage_wait_valid(descriptor, command_id, expected_bytes, slot_index))
        return kNdsAicpuInvalidArgument;
    const NdsStorageSlotDescriptor *slot = nds_storage_slot(descriptor, slot_index);
    if (slot == nullptr)
        return kNdsAicpuInvalidArgument;
    auto *completion = reinterpret_cast<volatile uint8_t *>(slot->completion_buffer.address);
    for (;;) {
        uint8_t observed[nds::kStorageCompletionBytes]{};
        for (uint32_t index = 0U; index < sizeof(observed); ++index) observed[index] = completion[index];
        NdsAicpuBarrier();
        nds::StorageCompletion decoded{};
        if (nds::deserialize_storage_completion(observed, sizeof(observed), &decoded) != nds::StorageSerdeResult::Ok ||
            decoded.state != nds::StorageCompletionState::Complete)
            continue;
        if (decoded.command_id != command_id || decoded.bytes_transferred != expected_bytes) {
            NdsAicpuSetReturnValue(return_value, NDS_OPERATION_INVALID_ARGUMENT);
            return kNdsAicpuSuccess;
        }
        NdsAicpuSetReturnValue(return_value, decoded.status == nds::StorageStatus::Success
                                                 ? NDS_OPERATION_SUCCESS
                                                 : NDS_OPERATION_PROVIDER_FAILED);
        return kNdsAicpuSuccess;
    }
}

}  // namespace

extern "C" uint32_t nds_aicpu_storage_read(const NdsStorageDescriptor *descriptor,
                                           const nds::StorageReadCommand *command, int32_t *return_value) {
    if (!nds_storage_read_valid(descriptor, command)) {
        if (return_value != nullptr)
            NdsAicpuSetReturnValue(return_value, NDS_OPERATION_INVALID_ARGUMENT);
        return return_value == nullptr ? kNdsAicpuInvalidArgument : kNdsAicpuSuccess;
    }
    return execute(descriptor, command, nds::serialize_storage_read, return_value);
}

extern "C" uint32_t nds_aicpu_storage_write(const NdsStorageDescriptor *descriptor,
                                            const nds::StorageWriteCommand *command, int32_t *return_value) {
    if (!nds_storage_write_valid(descriptor, command)) {
        if (return_value != nullptr)
            NdsAicpuSetReturnValue(return_value, NDS_OPERATION_INVALID_ARGUMENT);
        return return_value == nullptr ? kNdsAicpuInvalidArgument : kNdsAicpuSuccess;
    }
    return execute(descriptor, command, nds::serialize_storage_write, return_value);
}

extern "C" uint32_t nds_aicpu_storage_batch_read(const NdsStorageDescriptor *descriptor,
                                                 const nds::StorageBatchReadCommand *command, int32_t *return_value) {
    if (!nds_storage_batch_read_valid(descriptor, command)) {
        if (return_value != nullptr)
            NdsAicpuSetReturnValue(return_value, NDS_OPERATION_INVALID_ARGUMENT);
        return return_value == nullptr ? kNdsAicpuInvalidArgument : kNdsAicpuSuccess;
    }
    return execute(descriptor, command, nds::serialize_storage_batch_read, return_value);
}

extern "C" uint32_t nds_aicpu_storage_batch_write(const NdsStorageDescriptor *descriptor,
                                                  const nds::StorageBatchWriteCommand *command, int32_t *return_value) {
    if (!nds_storage_batch_write_valid(descriptor, command)) {
        if (return_value != nullptr)
            NdsAicpuSetReturnValue(return_value, NDS_OPERATION_INVALID_ARGUMENT);
        return return_value == nullptr ? kNdsAicpuInvalidArgument : kNdsAicpuSuccess;
    }
    return execute(descriptor, command, nds::serialize_storage_batch_write, return_value);
}

extern "C" uint32_t nds_aicpu_storage_wait(const NdsStorageDescriptor *descriptor, uint64_t command_id,
                                           uint64_t expected_bytes, uint32_t slot_index, int32_t *return_value) {
    return wait_for_completion(descriptor, command_id, expected_bytes, slot_index, return_value);
}
