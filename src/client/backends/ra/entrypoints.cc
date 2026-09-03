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

template <typename Args>
int storage_operation(const Args *args, nds::Result<void> (*operation)(const nds::RaStorageContext &, const Args &)) {
    if (args == nullptr || !nds_storage_descriptor_valid(&args->storage) ||
        !nds_storage_slot_id_valid(&args->storage, args->operation.slot_id))
        return -1;
    const NdsStorageDescriptor *descriptor = &args->storage;
    const std::uint32_t slot_index = nds_storage_slot_id_index(args->operation.slot_id);
    const NdsStorageSlotDescriptor *slot = nds_storage_slot(descriptor, slot_index);
    if (slot == nullptr)
        return -1;
    const NdsQpDescriptor *qp = nds_transport_qp(&descriptor->transport, slot->qp_index);
    NdsTransportQpState *transport_state = nds_transport_qp_state(&descriptor->transport, slot->qp_index);
    NdsStorageState *storage_state = nds_storage_state(descriptor, slot_index);
    if (slot == nullptr || qp == nullptr || transport_state == nullptr || storage_state == nullptr)
        return -1;
    const auto connection = host_connection(*qp);
    if (!connection.ok())
        return -1;
    const nds::RaStorageContext host_context{
        {connection.value().runtime, connection.value().qp},
        transport_state,
        storage_state,
        reinterpret_cast<void *>(slot->command_buffer.address),
        {slot->command_buffer.address, slot->command_buffer.length, slot->command_buffer.local_key},
        reinterpret_cast<void *>(slot->completion_buffer.address),
        {slot->completion_buffer.address, slot->completion_buffer.length, slot->completion_buffer.local_key},
        descriptor->capacity,
        slot_index};
    return operation(host_context, *args).ok() ? 0 : -1;
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

extern "C" int nds_ra_backend_storage_bootstrap(const NdsStorageBootstrapDescriptor *bootstrap) {
    if (bootstrap == nullptr || bootstrap->transport.qp_count == 0U || bootstrap->bootstrap.address == 0U)
        return -1;
    const NdsQpDescriptor *qp = nds_transport_qp(&bootstrap->transport, 0U);
    NdsTransportQpState *state = nds_transport_qp_state(&bootstrap->transport, 0U);
    if (qp == nullptr || state == nullptr)
        return -1;
    const auto connection = host_connection(*qp);
    return connection.ok() && nds::NdsRaStorageBootstrap(connection.value(), state, bootstrap->bootstrap).ok() ? 0 : -1;
}

extern "C" int nds_ra_backend_storage_read(const NdsStorageOperationArgs *args) {
    return storage_operation(args, nds::NdsRaStorageRead);
}

extern "C" int nds_ra_backend_storage_write(const NdsStorageOperationArgs *args) {
    return storage_operation(args, nds::NdsRaStorageWrite);
}

extern "C" int nds_ra_backend_storage_batch_read(const NdsStorageBatchOperationArgs *args) {
    return storage_operation(args, nds::NdsRaStorageBatchRead);
}

extern "C" int nds_ra_backend_storage_batch_write(const NdsStorageBatchOperationArgs *args) {
    return storage_operation(args, nds::NdsRaStorageBatchWrite);
}

extern "C" int nds_ra_backend_storage_wait(const NdsStorageDescriptor *descriptor, std::uint32_t slot_id) {
    if (descriptor == nullptr || !nds_storage_wait_valid(descriptor, slot_id))
        return -1;
    const std::uint32_t slot_index = nds_storage_slot_id_index(slot_id);
    const NdsStorageSlotDescriptor *slot = nds_storage_slot(descriptor, slot_index);
    const NdsQpDescriptor *qp = nds_transport_qp(&descriptor->transport, slot->qp_index);
    const auto connection =
        qp == nullptr
            ? nds::Result<nds::RaConnection>(nds::Error{nds::ErrorCode::kInvalidArgument, "invalid storage wait QP"})
            : host_connection(*qp);
    if (!connection.ok())
        return -1;
    const nds::RaStorageContext context{
        {connection.value().runtime, connection.value().qp},
        nds_transport_qp_state(&descriptor->transport, slot->qp_index),
        nds_storage_state(descriptor, slot_index),
        reinterpret_cast<void *>(slot->command_buffer.address),
        {slot->command_buffer.address, slot->command_buffer.length, slot->command_buffer.local_key},
        reinterpret_cast<void *>(slot->completion_buffer.address),
        {slot->completion_buffer.address, slot->completion_buffer.length, slot->completion_buffer.local_key},
        descriptor->capacity,
        slot_index};
    const auto completion = nds::NdsRaStorageWait(context);
    if (completion.ok())
        return completion.value().status == nds::StorageStatus::Success ? 0 : -1;
    return completion.error().message == "storage completion is pending" ? 1 : -1;
}
