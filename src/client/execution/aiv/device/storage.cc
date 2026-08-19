#include "api.h"
#include "internal.h"
#include "nds/device_storage.h"

namespace {
__aicore__ inline void StoreBytes(__gm__ uint8_t *destination, const uint8_t *source, uint32_t length) {
    for (uint32_t index = 0U; index < length; ++index) destination[index] = source[index];
    NdsAivCacheSync(destination, length);
}

__aicore__ inline void WriteBe16(uint8_t *destination, uint16_t value) {
    destination[0] = static_cast<uint8_t>(value >> 8);
    destination[1] = static_cast<uint8_t>(value);
}

__aicore__ inline void WriteBe32(uint8_t *destination, uint32_t value) {
    destination[0] = static_cast<uint8_t>(value >> 24);
    destination[1] = static_cast<uint8_t>(value >> 16);
    destination[2] = static_cast<uint8_t>(value >> 8);
    destination[3] = static_cast<uint8_t>(value);
}

__aicore__ inline void WriteBe64(uint8_t *destination, uint64_t value) {
    WriteBe32(destination, static_cast<uint32_t>(value >> 32));
    WriteBe32(destination + 4, static_cast<uint32_t>(value));
}

__aicore__ inline uint16_t ReadBe16(__gm__ const uint8_t *source) {
    return static_cast<uint16_t>((static_cast<uint16_t>(source[0]) << 8) | source[1]);
}

__aicore__ inline uint32_t ReadBe32(__gm__ const uint8_t *source) {
    return (static_cast<uint32_t>(source[0]) << 24) | (static_cast<uint32_t>(source[1]) << 16) |
           (static_cast<uint32_t>(source[2]) << 8) | source[3];
}

__aicore__ inline uint64_t ReadBe64(__gm__ const uint8_t *source) {
    return (static_cast<uint64_t>(ReadBe32(source)) << 32) | ReadBe32(source + 4);
}

__aicore__ inline bool StorageValid(__gm__ const nds_device_storage *storage, __gm__ const nds_device_storage_io *io) {
    const uint32_t command_length = storage->command.length;
    const uint32_t completion_length = storage->completion.length;
    const uint32_t io_length = io->length;
    const uint64_t offset = io->offset;
    const uint64_t capacity = storage->capacity;
    const uint16_t operation = io->operation;
    if (storage->abi_version != NDS_DEVICE_STORAGE_ABI_VERSION || storage->size != sizeof(nds_device_storage) ||
        storage->transport.abi_version != NDS_DEVICE_TRANSPORT_ABI_VERSION ||
        storage->transport.size != sizeof(nds_device_transport) || storage->request_id == 0U ||
        storage->command.address == 0U || storage->command.local_key == 0U ||
        command_length < sizeof(nds_protocol_command_wire) || storage->completion.address == 0U ||
        storage->completion.local_key == 0U || completion_length < sizeof(nds_protocol_completion_wire) ||
        io_length == 0U || io->data.address == 0U || io->data.local_key == 0U || io->data_rkey == 0U ||
        io->data.length < io_length || (operation != NDS_PROTOCOL_READ && operation != NDS_PROTOCOL_WRITE)) {
        return false;
    }
    if (offset > capacity || io_length > capacity - offset)
        return false;
    return true;
}

__aicore__ inline void EncodePending(uint64_t request_id, uint8_t *wire) {
    for (uint32_t index = 0U; index < sizeof(nds_protocol_completion_wire); ++index) wire[index] = 0U;
    WriteBe32(wire, NDS_PROTOCOL_COMPLETION_MAGIC);
    WriteBe16(wire + 4, NDS_PROTOCOL_VERSION);
    WriteBe16(wire + 6, NDS_PROTOCOL_COMPLETION_PENDING);
    WriteBe16(wire + 8, NDS_PROTOCOL_SUCCESS);
    WriteBe64(wire + 12, request_id);
}

__aicore__ inline void EncodeCommand(__gm__ const nds_device_storage *storage, __gm__ const nds_device_storage_io *io,
                                     uint8_t *wire) {
    const uint16_t operation = io->operation;
    const uint32_t access =
        operation == NDS_PROTOCOL_READ ? NDS_PROTOCOL_ACCESS_REMOTE_WRITE : NDS_PROTOCOL_ACCESS_REMOTE_READ;
    for (uint32_t index = 0U; index < sizeof(nds_protocol_command_wire); ++index) wire[index] = 0U;
    WriteBe32(wire, NDS_PROTOCOL_COMMAND_MAGIC);
    WriteBe16(wire + 4, NDS_PROTOCOL_VERSION);
    WriteBe16(wire + 6, operation);
    WriteBe64(wire + 8, storage->request_id);
    WriteBe64(wire + 16, io->offset);
    WriteBe64(wire + 24, io->length);
    WriteBe64(wire + 32, io->data.address);
    WriteBe64(wire + 40, io->data.length);
    WriteBe32(wire + 48, io->data_rkey);
    WriteBe32(wire + 52, access);
}

__aicore__ inline bool CompletionDone(__gm__ const uint8_t *wire, uint64_t request_id, uint64_t expected_bytes) {
    return ReadBe32(wire) == NDS_PROTOCOL_COMPLETION_MAGIC && ReadBe16(wire + 4) == NDS_PROTOCOL_VERSION &&
           ReadBe16(wire + 6) == NDS_PROTOCOL_COMPLETION_COMPLETE && ReadBe16(wire + 8) == NDS_PROTOCOL_SUCCESS &&
           ReadBe64(wire + 12) == request_id && ReadBe64(wire + 20) == expected_bytes;
}

__aicore__ inline void Execute(__gm__ const nds_device_storage *storage, __gm__ const nds_device_storage_io *io,
                               TBuf<> *scratch, __gm__ nds_device_operation_result *result) {
    if (result == nullptr)
        return;
    if (storage == nullptr || io == nullptr || !StorageValid(storage, io)) {
        NdsAivSetResult(result, NDS_DEVICE_OPERATION_INVALID_ARGUMENT);
        return;
    }
    const uint64_t request_id = storage->request_id;
    const uint64_t command_address = storage->command.address;
    const uint32_t command_lkey = storage->command.local_key;
    const uint64_t completion_address = storage->completion.address;
    const uint32_t io_length = io->length;
    uint8_t pending[sizeof(nds_protocol_completion_wire)];
    uint8_t command[sizeof(nds_protocol_command_wire)];
    EncodePending(request_id, pending);
    EncodeCommand(storage, io, command);
    StoreBytes(reinterpret_cast<__gm__ uint8_t *>(completion_address), pending, sizeof(pending));
    StoreBytes(reinterpret_cast<__gm__ uint8_t *>(command_address), command, sizeof(command));
    nds_device_send_wr wr{};
    wr.wr_id = request_id;
    wr.opcode = NDS_DEVICE_WR_SEND;
    wr.flags = NDS_DEVICE_SEND_SIGNALED;
    wr.local.address = command_address;
    wr.local.length = sizeof(command);
    wr.local.local_key = command_lkey;
    NdsAivPostSendImpl(&storage->transport.control_qp, &wr, scratch, result);
    if (result->status != NDS_DEVICE_OPERATION_SUCCESS)
        return;
    __gm__ const uint8_t *completion = reinterpret_cast<__gm__ const uint8_t *>(completion_address);
    for (;;) {
        NdsAivCacheSync(reinterpret_cast<__gm__ uint8_t *>(completion_address), sizeof(nds_protocol_completion_wire));
        if (CompletionDone(completion, request_id, io_length)) {
            NdsAivSetResult(result, NDS_DEVICE_OPERATION_SUCCESS);
            return;
        }
    }
}
}  // namespace

NDS_AIV_DEVICE_API_LINKAGE __aicore__ void NdsAivStorageReadImpl(__gm__ const nds_device_storage *storage,
                                                                 __gm__ const nds_device_storage_io *io,
                                                                 TBuf<> *scratch,
                                                                 __gm__ nds_device_operation_result *result) {
    Execute(storage, io, scratch, result);
}

NDS_AIV_DEVICE_API_LINKAGE __aicore__ void NdsAivStorageWriteImpl(__gm__ const nds_device_storage *storage,
                                                                  __gm__ const nds_device_storage_io *io,
                                                                  TBuf<> *scratch,
                                                                  __gm__ nds_device_operation_result *result) {
    Execute(storage, io, scratch, result);
}
