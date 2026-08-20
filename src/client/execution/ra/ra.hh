#ifndef NDS_RA_HPP
#define NDS_RA_HPP

#include "nds/device_transport.h"
#include "nds/device_storage.h"
#include "nds/storage_protocol.hh"
#include "endpoint.hh"
#include "runtime.hh"
#include "nds/result.hh"

#include <cstdint>

namespace nds {

struct RaConnection {
    client::Runtime *runtime{};
    client::QueuePair *qp{};
};

struct RaStorageContext {
    RaConnection connection{};
    void *command_device{};
    NdsRaSge command_buffer{};
    void *completion_device{};
    NdsRaSge completion{};
    std::uint64_t capacity{};
};

/* Verbs layer. PostSend returns the RA doorbell metadata to its caller. */
Result<NdsRaSendResponse> NdsRaPostSend(client::QueuePair *qp, const NdsDeviceSendWr &wr);
Result<void> NdsRaPostRecv(client::QueuePair *qp, const NdsDeviceRecvWr &wr);
Result<std::uint32_t> NdsRaPollCq(client::QueuePair *qp, std::uint32_t queue_kind,
                                  NdsDeviceCompletionOutput *output);

/* Connection layer. Send/Read/Write post and ring the runtime doorbell. */
Result<void> NdsRaRdmaSend(const RaConnection &connection, const NdsDeviceTransfer &transfer);
Result<void> NdsRaRdmaRecv(const RaConnection &connection, const NdsDeviceTransfer &transfer);
Result<void> NdsRaRdmaRead(const RaConnection &connection, const NdsDeviceTransfer &transfer);
Result<void> NdsRaRdmaWrite(const RaConnection &connection, const NdsDeviceTransfer &transfer);

/* Storage layer. Completion is the CPU-written NDS protocol record. */
Result<void> NdsRaStorageRead(const RaStorageContext &context, const StorageReadCommand &command);
Result<void> NdsRaStorageWrite(const RaStorageContext &context, const StorageWriteCommand &command);
Result<void> NdsRaStorageBatchRead(const RaStorageContext &context, const StorageBatchReadCommand &command);
Result<void> NdsRaStorageBatchWrite(const RaStorageContext &context, const StorageBatchWriteCommand &command);

}  // namespace nds

#endif
