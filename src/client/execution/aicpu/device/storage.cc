#include "api.h"
#include "internal.h"
#include "nds/device_storage.h"

namespace {
uint32_t execute(const nds_device_storage *storage, const nds_device_storage_io *io,
                 nds_device_operation_result *result) {
    if (result == nullptr)
        return kNdsAicpuInvalidArgument;
    if (!nds_device_storage_valid(storage, io)) {
        NdsAicpuSetResult(result, NDS_DEVICE_OPERATION_INVALID_ARGUMENT, NDS_DEVICE_OPERATION_PATH_NONE, 0);
        return kNdsAicpuSuccess;
    }
    nds_protocol_completion_wire pending{};
    nds_protocol_command_wire command{};
    nds_device_storage_encode_pending(storage, &pending);
    nds_device_storage_encode_command(storage, io, &command);
    *reinterpret_cast<nds_protocol_completion_wire *>(storage->completion.address) = pending;
    *reinterpret_cast<nds_protocol_command_wire *>(storage->command.address) = command;
    NdsAicpuBarrier();
    nds_device_transfer transfer{};
    transfer.wr_id = storage->request_id;
    transfer.local.address = storage->command.address;
    transfer.local.length = sizeof(command);
    transfer.local.local_key = storage->command.local_key;
    const uint32_t sent = NdsAicpuRdmaSendImpl(&storage->transport, &transfer, result);
    if (sent != kNdsAicpuSuccess || result->status != NDS_DEVICE_OPERATION_SUCCESS)
        return sent;
    auto *completion = reinterpret_cast<volatile nds_protocol_completion_wire *>(storage->completion.address);
    for (;;) {
        nds_protocol_completion_wire observed{};
        observed = *const_cast<nds_protocol_completion_wire *>(completion);
        NdsAicpuBarrier();
        if (nds_device_storage_completion_done(&observed, storage->request_id, io->length)) {
            NdsAicpuSetResult(result, NDS_DEVICE_OPERATION_SUCCESS, NDS_DEVICE_OPERATION_PATH_DIRECT, 0);
            return kNdsAicpuSuccess;
        }
    }
}
}  // namespace

extern "C" uint32_t NdsAicpuStorageReadImpl(const nds_device_storage *storage, const nds_device_storage_io *io,
                                            nds_device_operation_result *result) {
    return execute(storage, io, result);
}

extern "C" uint32_t NdsAicpuStorageWriteImpl(const nds_device_storage *storage, const nds_device_storage_io *io,
                                             nds_device_operation_result *result) {
    return execute(storage, io, result);
}
