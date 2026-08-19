#include "api.h"
#include "internal.h"

namespace {
bool valid_transport(const nds_device_transport *transport, const nds_device_transfer *transfer,
                      nds_device_operation_result *result) {
    return transport != nullptr && transfer != nullptr && result != nullptr &&
           transport->abi_version == NDS_DEVICE_TRANSPORT_ABI_VERSION && transport->size == sizeof(*transport);
}

uint32_t post(const nds_device_transport *transport, const nds_device_transfer *transfer, uint32_t opcode,
              nds_device_operation_result *result) {
    if (!valid_transport(transport, transfer, result)) return kNdsAicpuInvalidArgument;
    nds_device_send_wr wr{};
    nds_device_build_send_wr(transfer, opcode, &wr);
    return NdsAicpuPostSendImpl(&transport->control_qp, &wr, result);
}
}  // namespace

extern "C" uint32_t NdsAicpuRdmaSendImpl(
    const nds_device_transport *transport, const nds_device_transfer *transfer,
    nds_device_operation_result *result) {
    return post(transport, transfer, NDS_DEVICE_WR_SEND, result);
}

extern "C" uint32_t NdsAicpuRdmaRecvImpl(
    const nds_device_transport *transport, const nds_device_transfer *transfer,
    nds_device_operation_result *result) {
    if (!valid_transport(transport, transfer, result)) return kNdsAicpuInvalidArgument;
    nds_device_recv_wr wr{};
    nds_device_build_recv_wr(transfer, &wr);
    return NdsAicpuPostRecvImpl(&transport->control_qp, &wr, result);
}

extern "C" uint32_t NdsAicpuRdmaReadImpl(
    const nds_device_transport *transport, const nds_device_transfer *transfer,
    nds_device_operation_result *result) {
    return post(transport, transfer, NDS_DEVICE_WR_RDMA_READ, result);
}

extern "C" uint32_t NdsAicpuRdmaWriteImpl(
    const nds_device_transport *transport, const nds_device_transfer *transfer,
    nds_device_operation_result *result) {
    return post(transport, transfer, NDS_DEVICE_WR_RDMA_WRITE, result);
}
