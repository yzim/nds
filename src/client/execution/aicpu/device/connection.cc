#include "api.h"
#include "internal.h"

namespace {
bool valid_transport(const NdsDeviceTransport *transport, const NdsDeviceSendWr *wr, int32_t *return_value) {
    return transport != nullptr && wr != nullptr && return_value != nullptr;
}

uint32_t post(const NdsDeviceTransport *transport, const NdsDeviceSendWr *wr, int32_t *return_value) {
    if (!valid_transport(transport, wr, return_value))
        return kNdsAicpuInvalidArgument;
    return nds_aicpu_post_send(&transport->control_qp, wr, return_value);
}
}  // namespace

extern "C" uint32_t nds_aicpu_rdma_send(const NdsDeviceTransport *transport, const NdsDeviceSendWr *wr,
                                        int32_t *return_value) {
    return post(transport, wr, return_value);
}

extern "C" uint32_t nds_aicpu_rdma_recv(const NdsDeviceTransport *transport, const NdsDeviceRecvWr *wr,
                                        int32_t *return_value) {
    if (transport == nullptr || wr == nullptr || return_value == nullptr)
        return kNdsAicpuInvalidArgument;
    return nds_aicpu_post_recv(&transport->control_qp, wr, return_value);
}

extern "C" uint32_t nds_aicpu_rdma_read(const NdsDeviceTransport *transport, const NdsDeviceSendWr *wr,
                                        int32_t *return_value) {
    return post(transport, wr, return_value);
}

extern "C" uint32_t nds_aicpu_rdma_write(const NdsDeviceTransport *transport, const NdsDeviceSendWr *wr,
                                         int32_t *return_value) {
    return post(transport, wr, return_value);
}
