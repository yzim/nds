#include "backend_abi.hh"

#include "ra.hh"

namespace {

nds::Result<nds::RaConnection> host_connection(const NdsQpDescriptor &qp) {
    auto *runtime = reinterpret_cast<nds::client::Runtime *>(qp.host_runtime_address);
    auto *queue_pair = reinterpret_cast<nds::client::QueuePair *>(qp.host_qp_address);
    if (runtime == nullptr || queue_pair == nullptr)
        return nds::Error{nds::ErrorCode::kInvalidArgument, "RA QP is missing host handles"};
    return nds::RaConnection{runtime, queue_pair};
}

template <typename WorkRequest>
int rdma_operation(const NdsTransportDescriptor *transport, std::uint32_t queue_index, const WorkRequest *wr,
                   nds::Result<void> (*operation)(const nds::RaConnection &, NdsTransportQpState *,
                                                  const WorkRequest &)) {
    if (transport == nullptr || wr == nullptr)
        return -1;
    const NdsQpDescriptor *qp = nds_transport_qp(transport, queue_index);
    NdsTransportQpState *state = nds_transport_qp_state(transport, queue_index);
    if (qp == nullptr || state == nullptr)
        return -1;
    const auto connection = host_connection(*qp);
    if (!connection.ok())
        return -1;
    return operation(connection.value(), state, *wr).ok() ? 0 : -1;
}

template <typename Command>
int storage_operation(const NdsStorageDescriptor *descriptor, const Command *command,
                      nds::Result<void> (*operation)(const nds::RaStorageContext &, const Command &)) {
    if (descriptor == nullptr || command == nullptr)
        return -1;
    const NdsStorageSlotDescriptor *slot = nds_storage_slot(descriptor, command->slot_index);
    if (slot == nullptr || !nds_storage_slot_valid(descriptor, command->slot_index))
        return -1;
    const NdsQpDescriptor *qp = nds_transport_qp(&descriptor->transport, slot->qp_index);
    NdsTransportQpState *state = nds_transport_qp_state(&descriptor->transport, slot->qp_index);
    if (slot == nullptr || qp == nullptr || state == nullptr)
        return -1;
    const auto connection = host_connection(*qp);
    if (!connection.ok())
        return -1;
    const nds::RaStorageContext host_context{
        {connection.value().runtime, connection.value().qp},
        state,
        reinterpret_cast<void *>(slot->command_buffer.address),
        {slot->command_buffer.address, slot->command_buffer.length, slot->command_buffer.local_key},
        reinterpret_cast<void *>(slot->completion_buffer.address),
        {slot->completion_buffer.address, slot->completion_buffer.length, slot->completion_buffer.local_key},
        descriptor->capacity};
    return operation(host_context, *command).ok() ? 0 : -1;
}

}  // namespace

extern "C" int nds_ra_backend_post_send(const NdsQpDescriptor *qp, const NdsSendWr *wr, void *stream) {
    if (qp == nullptr || wr == nullptr)
        return -1;
    const auto connection = host_connection(*qp);
    return connection.ok() && nds::NdsRaPostSend(connection.value().runtime, connection.value().qp, *wr, stream).ok()
               ? 0
               : -1;
}

extern "C" int nds_ra_backend_post_recv(const NdsQpDescriptor *qp, const NdsRecvWr *wr) {
    if (qp == nullptr || wr == nullptr)
        return -1;
    const auto connection = host_connection(*qp);
    return connection.ok() && nds::NdsRaPostRecv(connection.value().qp, *wr).ok() ? 0 : -1;
}

extern "C" int nds_ra_backend_poll_cq(const NdsQpDescriptor *qp, std::uint32_t send_cq, std::uint32_t max_completions,
                                      NdsWc *wc) {
    if (qp == nullptr)
        return -1;
    auto *queue_pair = reinterpret_cast<nds::client::QueuePair *>(qp->host_qp_address);
    if (queue_pair == nullptr)
        return -1;
    const auto result = nds::NdsRaPollCq(queue_pair, send_cq != 0U, max_completions, wc);
    return result.ok() ? static_cast<int>(result.value()) : -1;
}

extern "C" int nds_ra_backend_rdma_send(const NdsTransportDescriptor *transport, std::uint32_t queue_index,
                                        const NdsSendWr *wr) {
    return rdma_operation(transport, queue_index, wr, nds::NdsRaRdmaSend);
}

extern "C" int nds_ra_backend_rdma_recv(const NdsTransportDescriptor *transport, std::uint32_t queue_index,
                                        const NdsRecvWr *wr) {
    return rdma_operation(transport, queue_index, wr, nds::NdsRaRdmaRecv);
}

extern "C" int nds_ra_backend_rdma_read(const NdsTransportDescriptor *transport, std::uint32_t queue_index,
                                        const NdsSendWr *wr) {
    return rdma_operation(transport, queue_index, wr, nds::NdsRaRdmaRead);
}

extern "C" int nds_ra_backend_rdma_write(const NdsTransportDescriptor *transport, std::uint32_t queue_index,
                                         const NdsSendWr *wr) {
    return rdma_operation(transport, queue_index, wr, nds::NdsRaRdmaWrite);
}

extern "C" int nds_ra_backend_storage_read(const NdsStorageDescriptor *descriptor,
                                           const nds::StorageReadCommand *command) {
    return storage_operation(descriptor, command, nds::NdsRaStorageRead);
}

extern "C" int nds_ra_backend_storage_write(const NdsStorageDescriptor *descriptor,
                                            const nds::StorageWriteCommand *command) {
    return storage_operation(descriptor, command, nds::NdsRaStorageWrite);
}

extern "C" int nds_ra_backend_storage_batch_read(const NdsStorageDescriptor *descriptor,
                                                 const nds::StorageBatchReadCommand *command) {
    return storage_operation(descriptor, command, nds::NdsRaStorageBatchRead);
}

extern "C" int nds_ra_backend_storage_batch_write(const NdsStorageDescriptor *descriptor,
                                                  const nds::StorageBatchWriteCommand *command) {
    return storage_operation(descriptor, command, nds::NdsRaStorageBatchWrite);
}
