#include "nds_aicpu_device_api.h"
#include "nds_aicpu_device_internal.h"

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
    return NdsAicpuPostSend(&connection->qp, &wr, result);
}
}  // namespace

extern "C" __attribute__((visibility("default"))) uint32_t NdsAicpuRdmaSend(
    const nds_device_connection *connection, const nds_device_transfer *transfer,
    nds_device_operation_result *result) {
    return post(connection, transfer, NDS_DEVICE_WR_SEND, result);
}

extern "C" __attribute__((visibility("default"))) uint32_t NdsAicpuRdmaRecv(
    const nds_device_connection *connection, const nds_device_transfer *transfer,
    nds_device_operation_result *result) {
    if (!valid_connection(connection, transfer, result)) return kNdsAicpuInvalidArgument;
    nds_device_recv_wr wr{};
    nds_device_build_recv_wr(transfer, &wr);
    return NdsAicpuPostRecv(&connection->qp, &wr, result);
}

extern "C" __attribute__((visibility("default"))) uint32_t NdsAicpuRdmaRead(
    const nds_device_connection *connection, const nds_device_transfer *transfer,
    nds_device_operation_result *result) {
    return post(connection, transfer, NDS_DEVICE_WR_RDMA_READ, result);
}

extern "C" __attribute__((visibility("default"))) uint32_t NdsAicpuRdmaWrite(
    const nds_device_connection *connection, const nds_device_transfer *transfer,
    nds_device_operation_result *result) {
    return post(connection, transfer, NDS_DEVICE_WR_RDMA_WRITE, result);
}
