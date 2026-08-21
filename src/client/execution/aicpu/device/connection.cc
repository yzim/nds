#include "api.h"
#include "internal.h"

namespace {
bool valid_transport(const NdsDeviceTransport *transport, const NdsDeviceSendWr *wr, int32_t *return_value) {
    return transport != nullptr && wr != nullptr && return_value != nullptr;
}

uint32_t post(const NdsDeviceTransport *transport, const NdsDeviceSendWr *wr, int32_t *return_value) {
    if (!valid_transport(transport, wr, return_value))
        return kNdsAicpuInvalidArgument;
    return NdsAicpuPostSendImpl(&transport->control_qp, wr, return_value);
}
}  // namespace

extern "C" uint32_t NdsAicpuRdmaSendImpl(const NdsDeviceTransport *transport, const NdsDeviceSendWr *wr,
                                         int32_t *return_value) {
    return post(transport, wr, return_value);
}

extern "C" uint32_t NdsAicpuRdmaRecvImpl(const NdsDeviceTransport *transport, const NdsDeviceRecvWr *wr,
                                         int32_t *return_value) {
    if (transport == nullptr || wr == nullptr || return_value == nullptr)
        return kNdsAicpuInvalidArgument;
    return NdsAicpuPostRecvImpl(&transport->control_qp, wr, return_value);
}

extern "C" uint32_t NdsAicpuRdmaReadImpl(const NdsDeviceTransport *transport, const NdsDeviceSendWr *wr,
                                         int32_t *return_value) {
    return post(transport, wr, return_value);
}

extern "C" uint32_t NdsAicpuRdmaWriteImpl(const NdsDeviceTransport *transport, const NdsDeviceSendWr *wr,
                                          int32_t *return_value) {
    return post(transport, wr, return_value);
}
