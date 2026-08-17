#include "api.h"
#include "internal.h"

namespace {
bool valid_connection(const nds_device_connection *connection, const nds_device_transfer *transfer,
                      nds_device_operation_result *result) {
    return connection != nullptr && transfer != nullptr && result != nullptr &&
           connection->abi_version == NDS_DEVICE_CONNECTION_ABI_VERSION &&
           connection->size == sizeof(*connection);
}

uint32_t post(const nds_device_connection *connection, const nds_device_transfer *transfer, uint32_t opcode,
              nds_device_operation_result *result) {
    if (!valid_connection(connection, transfer, result)) return kNdsAicpuInvalidArgument;
    nds_device_send_wr wr{};
    nds_device_build_send_wr(transfer, opcode, &wr);
    return NdsAicpuPostSendImpl(&connection->qp, &wr, result);
}
}  // namespace

extern "C" uint32_t NdsAicpuRdmaSendImpl(
    const nds_device_connection *connection, const nds_device_transfer *transfer,
    nds_device_operation_result *result) {
    return post(connection, transfer, NDS_DEVICE_WR_SEND, result);
}

extern "C" uint32_t NdsAicpuRdmaRecvImpl(
    const nds_device_connection *connection, const nds_device_transfer *transfer,
    nds_device_operation_result *result) {
    if (!valid_connection(connection, transfer, result)) return kNdsAicpuInvalidArgument;
    nds_device_recv_wr wr{};
    nds_device_build_recv_wr(transfer, &wr);
    return NdsAicpuPostRecvImpl(&connection->qp, &wr, result);
}

extern "C" uint32_t NdsAicpuRdmaReadImpl(
    const nds_device_connection *connection, const nds_device_transfer *transfer,
    nds_device_operation_result *result) {
    return post(connection, transfer, NDS_DEVICE_WR_RDMA_READ, result);
}

extern "C" uint32_t NdsAicpuRdmaWriteImpl(
    const nds_device_connection *connection, const nds_device_transfer *transfer,
    nds_device_operation_result *result) {
    return post(connection, transfer, NDS_DEVICE_WR_RDMA_WRITE, result);
}
