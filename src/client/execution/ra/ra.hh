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

/* Verbs layer. PostSend posts and submits one RA work request. The prepare and
 * ring forms support a bounded multi-WR window with one final doorbell. */
Result<NdsRaSendResponse> NdsRaPrepareSend(client::QueuePair *qp, const NdsDeviceSendWr &wr);
Result<NdsRaSendResponse> NdsRaPrepareSend(client::QueuePair *qp, const NdsDeviceSendWr &wr, NdsRaSge *sge);
Result<void> NdsRaRingSend(client::Runtime *runtime, const NdsRaSendResponse &response);
Result<void> NdsRaPostSend(client::Runtime *runtime, client::QueuePair *qp, const NdsDeviceSendWr &wr);
Result<void> NdsRaPostRecv(client::QueuePair *qp, const NdsDeviceRecvWr &wr);
Result<std::uint32_t> NdsRaPollCq(client::QueuePair *qp, bool is_send_cq, std::uint32_t max_completions,
                                  NdsDeviceWc *wc);

/* Connection layer. Send/Read/Write build a work request and submit it. */
Result<void> NdsRaRdmaSend(const RaConnection &connection, const NdsDeviceSendWr &wr);
Result<void> NdsRaRdmaRecv(const RaConnection &connection, const NdsDeviceSendWr &wr);
Result<void> NdsRaRdmaRead(const RaConnection &connection, const NdsDeviceSendWr &wr);
Result<void> NdsRaRdmaWrite(const RaConnection &connection, const NdsDeviceSendWr &wr);

/* Storage layer. Completion is the CPU-written NDS protocol record. */
Result<void> NdsRaStorageRead(const RaStorageContext &context, const StorageReadCommand &command);
Result<void> NdsRaStorageWrite(const RaStorageContext &context, const StorageWriteCommand &command);
Result<void> NdsRaStorageBatchRead(const RaStorageContext &context, const StorageBatchReadCommand &command);
Result<void> NdsRaStorageBatchWrite(const RaStorageContext &context, const StorageBatchWriteCommand &command);

}  // namespace nds

#endif
