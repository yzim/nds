#include "backend_abi.hh"

#include "ra.hh"

namespace {

nds::Result<nds::RaConnection> host_connection(const NdsDeviceQp &qp) {
    auto *runtime = reinterpret_cast<nds::client::Runtime *>(qp.host_runtime_address);
    auto *queue_pair = reinterpret_cast<nds::client::QueuePair *>(qp.host_qp_address);
    if (runtime == nullptr || queue_pair == nullptr)
        return Error{nds::ErrorCode::kInvalidArgument, "RA QP is missing host handles"};
    return nds::RaConnection{runtime, queue_pair};
}

template <typename WorkRequest>
int rdma_operation(const NdsDeviceTransport *transport, const WorkRequest *wr,
                   nds::Result<void> (*operation)(const nds::RaConnection &, const WorkRequest &)) {
    if (transport == nullptr || wr == nullptr)
        return -1;
    const NdsDeviceQp *qp = nds_device_transport_qp(transport, 0U);
    if (qp == nullptr)
        return -1;
    const auto connection = host_connection(*qp);
    if (!connection)
        return -1;
    return operation(*connection, *wr) ? 0 : -1;
}

template <typename Command>
int storage_operation(const NdsDeviceStorageContext *context, const Command *command,
                      nds::Result<void> (*operation)(const nds::RaStorageContext &, const Command &)) {
    if (context == nullptr || command == nullptr)
        return -1;
    const NdsDeviceQp *qp = nds_device_transport_qp(&context->transport, 0U);
    if (qp == nullptr)
        return -1;
    const auto connection = host_connection(*qp);
    if (!connection)
        return -1;
    const nds::RaStorageContext host_context{
        {connection->runtime, connection->qp},
        reinterpret_cast<void *>(context->command_buffer.address),
        {context->command_buffer.address, context->command_buffer.length, context->command_buffer.local_key},
        reinterpret_cast<void *>(context->completion.address),
        {context->completion.address, context->completion.length, context->completion.local_key},
        context->capacity};
    return operation(host_context, *command) ? 0 : -1;
}

}  // namespace

extern "C" int nds_ra_backend_post_send(const NdsDeviceQp *qp, const NdsDeviceSendWr *wr) {
    if (qp == nullptr || wr == nullptr)
        return -1;
    const auto connection = host_connection(*qp);
    return connection && nds::NdsRaPostSend(connection->runtime, connection->qp, *wr) ? 0 : -1;
}

extern "C" int nds_ra_backend_poll_cq(const NdsDeviceQp *qp, std::uint32_t send_cq, std::uint32_t max_completions,
                                      NdsDeviceWc *wc) {
    if (qp == nullptr)
        return -1;
    auto *queue_pair = reinterpret_cast<nds::client::QueuePair *>(qp->host_qp_address);
    if (queue_pair == nullptr)
        return -1;
    const auto result = nds::NdsRaPollCq(queue_pair, send_cq != 0U, max_completions, wc);
    return result ? static_cast<int>(*result) : -1;
}

extern "C" int nds_ra_backend_rdma_send(const NdsDeviceTransport *transport, const NdsDeviceSendWr *wr) {
    return rdma_operation(transport, wr, nds::NdsRaRdmaSend);
}

extern "C" int nds_ra_backend_rdma_recv(const NdsDeviceTransport *transport, const NdsDeviceRecvWr *wr) {
    return rdma_operation(transport, wr, nds::NdsRaRdmaRecv);
}

extern "C" int nds_ra_backend_rdma_read(const NdsDeviceTransport *transport, const NdsDeviceSendWr *wr) {
    return rdma_operation(transport, wr, nds::NdsRaRdmaRead);
}

extern "C" int nds_ra_backend_rdma_write(const NdsDeviceTransport *transport, const NdsDeviceSendWr *wr) {
    return rdma_operation(transport, wr, nds::NdsRaRdmaWrite);
}

extern "C" int nds_ra_backend_storage_read(const NdsDeviceStorageContext *context,
                                           const nds::StorageReadCommand *command) {
    return storage_operation(context, command, nds::NdsRaStorageRead);
}

extern "C" int nds_ra_backend_storage_write(const NdsDeviceStorageContext *context,
                                            const nds::StorageWriteCommand *command) {
    return storage_operation(context, command, nds::NdsRaStorageWrite);
}

extern "C" int nds_ra_backend_storage_batch_read(const NdsDeviceStorageContext *context,
                                                 const nds::StorageBatchReadCommand *command) {
    return storage_operation(context, command, nds::NdsRaStorageBatchRead);
}

extern "C" int nds_ra_backend_storage_batch_write(const NdsDeviceStorageContext *context,
                                                  const nds::StorageBatchWriteCommand *command) {
    return storage_operation(context, command, nds::NdsRaStorageBatchWrite);
}
