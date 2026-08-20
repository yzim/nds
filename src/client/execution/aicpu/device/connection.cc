#include "api.h"
#include "internal.h"

namespace {
bool valid_transport(const NdsDeviceTransport *transport, const NdsDeviceTransfer *transfer,
                     NdsDeviceOperationResult *result) {
    return transport != nullptr && transfer != nullptr && result != nullptr &&
           transport->abi_version == NDS_DEVICE_TRANSPORT_ABI_VERSION && transport->size == sizeof(*transport);
}

uint32_t post(const NdsDeviceTransport *transport, const NdsDeviceTransfer *transfer, uint32_t opcode,
              NdsDeviceOperationResult *result) {
    if (!valid_transport(transport, transfer, result))
        return kNdsAicpuInvalidArgument;
    NdsDeviceSendWr wr{};
    nds_device_build_send_wr(transfer, opcode, &wr);
    return NdsAicpuPostSendImpl(&transport->control_qp, &wr, result);
}
}  // namespace

extern "C" uint32_t NdsAicpuRdmaSendImpl(const NdsDeviceTransport *transport, const NdsDeviceTransfer *transfer,
                                         NdsDeviceOperationResult *result) {
    return post(transport, transfer, NDS_DEVICE_WR_SEND, result);
}

extern "C" uint32_t NdsAicpuRdmaRecvImpl(const NdsDeviceTransport *transport, const NdsDeviceTransfer *transfer,
                                         NdsDeviceOperationResult *result) {
    if (!valid_transport(transport, transfer, result))
        return kNdsAicpuInvalidArgument;
    NdsDeviceRecvWr wr{};
    nds_device_build_recv_wr(transfer, &wr);
    return NdsAicpuPostRecvImpl(&transport->control_qp, &wr, result);
}

extern "C" uint32_t NdsAicpuRdmaReadImpl(const NdsDeviceTransport *transport, const NdsDeviceTransfer *transfer,
                                         NdsDeviceOperationResult *result) {
    return post(transport, transfer, NDS_DEVICE_WR_RDMA_READ, result);
}

extern "C" uint32_t NdsAicpuRdmaWriteImpl(const NdsDeviceTransport *transport, const NdsDeviceTransfer *transfer,
                                          NdsDeviceOperationResult *result) {
    return post(transport, transfer, NDS_DEVICE_WR_RDMA_WRITE, result);
}
