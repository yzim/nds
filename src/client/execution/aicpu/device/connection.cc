#include "api.h"
#include "internal.h"

namespace {
bool valid_transport(const NdsDeviceTransport *transport, const NdsDeviceSendWr *wr,
                     NdsDeviceOperationResult *result) {
    return transport != nullptr && wr != nullptr && result != nullptr &&
           transport->abi_version == NDS_DEVICE_TRANSPORT_ABI_VERSION && transport->size == sizeof(*transport);
}

uint32_t post(const NdsDeviceTransport *transport, const NdsDeviceSendWr *wr, NdsDeviceOperationResult *result) {
    if (!valid_transport(transport, wr, result))
        return kNdsAicpuInvalidArgument;
    return NdsAicpuPostSendImpl(&transport->control_qp, wr, result);
}
}  // namespace

extern "C" uint32_t NdsAicpuRdmaSendImpl(const NdsDeviceTransport *transport, const NdsDeviceSendWr *wr,
                                         NdsDeviceOperationResult *result) {
    return post(transport, wr, result);
}

extern "C" uint32_t NdsAicpuRdmaRecvImpl(const NdsDeviceTransport *transport, const NdsDeviceSendWr *wr,
                                         NdsDeviceOperationResult *result) {
    if (!valid_transport(transport, wr, result))
        return kNdsAicpuInvalidArgument;
    NdsDeviceRecvWr recv{};
    recv.wr_id = wr->wr_id;
    recv.local = wr->local;
    return NdsAicpuPostRecvImpl(&transport->control_qp, &recv, result);
}

extern "C" uint32_t NdsAicpuRdmaReadImpl(const NdsDeviceTransport *transport, const NdsDeviceSendWr *wr,
                                         NdsDeviceOperationResult *result) {
    return post(transport, wr, result);
}

extern "C" uint32_t NdsAicpuRdmaWriteImpl(const NdsDeviceTransport *transport, const NdsDeviceSendWr *wr,
                                          NdsDeviceOperationResult *result) {
    return post(transport, wr, result);
}
