#ifndef NDS_RA_HPP
#define NDS_RA_HPP

#include "backend_transport.h"
#include "backend_storage.h"
#include "storage_protocol.hh"
#include "endpoint.hh"
#include "runtime.hh"
#include "result.hh"

#include <cstdint>

namespace nds {

struct RaConnection {
    client::Runtime *runtime{};
    client::QueuePair *qp{};
};

struct RaStorageContext {
    RaConnection connection{};
    NdsTransportQpState *transport_state{};
    NdsStorageState *storage_state{};
    void *command_device{};
    Libra::Sge command_buffer{};
    void *completion_device{};
    Libra::Sge completion{};
    std::uint64_t capacity{};
    std::uint32_t slot_index{};
};

/* Verbs layer. A caller that owns a bounded verbs window may prepare several
 * WQEs and ring the final response once; PostSend remains the one-WR helper. */
Result<Libra::SendResponse> NdsRaPrepareSend(client::QueuePair *qp, const NdsSendWr &wr, Libra::Sge *sge);
Result<void> NdsRaRingSend(client::Runtime *runtime, const Libra::SendResponse &response, void *stream);
Result<void> NdsRaPostSend(client::Runtime *runtime, client::QueuePair *qp, const NdsSendWr &wr, void *stream);
Result<void> NdsRaPostRecv(client::QueuePair *qp, const NdsRecvWr &wr);
Result<std::uint32_t> NdsRaPollCq(client::QueuePair *qp, bool is_send_cq, std::uint32_t max_completions, NdsWc *wc);

/* Connection layer. Send/Read/Write build a work request and submit it. */
Result<void> NdsRaRdmaSend(const RaConnection &connection, NdsTransportQpState *state, const NdsSendWr &wr);
Result<void> NdsRaRdmaRecv(const RaConnection &connection, NdsTransportQpState *state, const NdsRecvWr &wr);
Result<void> NdsRaRdmaRead(const RaConnection &connection, NdsTransportQpState *state, const NdsSendWr &wr);
Result<void> NdsRaRdmaWrite(const RaConnection &connection, NdsTransportQpState *state, const NdsSendWr &wr);

/* Storage layer. Completion is the CPU-written NDS protocol record. */
Result<void> NdsRaStorageBootstrap(const RaConnection &connection, NdsTransportQpState *state, const NdsSge &bootstrap);
Result<void> NdsRaStorageRead(const RaStorageContext &context, const NdsStorageOperationArgs &args);
Result<void> NdsRaStorageWrite(const RaStorageContext &context, const NdsStorageOperationArgs &args);
Result<void> NdsRaStorageBatchRead(const RaStorageContext &context, const NdsStorageBatchOperationArgs &args);
Result<void> NdsRaStorageBatchWrite(const RaStorageContext &context, const NdsStorageBatchOperationArgs &args);
Result<StorageCompletion> NdsRaStorageWait(const RaStorageContext &context);

}  // namespace nds

#endif
