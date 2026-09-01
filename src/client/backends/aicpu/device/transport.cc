#include "api.h"
#include "internal.h"

namespace {
bool valid_transport(const NdsTransportDescriptor *transport, const NdsSendWr *wr, int32_t *return_value) {
    return transport != nullptr && wr != nullptr && return_value != nullptr;
}

uint32_t post(const NdsTransportDescriptor *transport, uint32_t queue_index, const NdsSendWr *wr,
              int32_t *return_value) {
    if (!valid_transport(transport, wr, return_value))
        return kNdsAicpuInvalidArgument;
    const NdsQpDescriptor *qp = nds_transport_qp(transport, queue_index);
    return qp == nullptr ? kNdsAicpuInvalidArgument : nds_aicpu_post_send(qp, wr, return_value);
}
}  // namespace

extern "C" uint32_t nds_aicpu_rdma_send(const NdsTransportDescriptor *transport, uint32_t queue_index,
                                        const NdsSendWr *wr, int32_t *return_value) {
    return post(transport, queue_index, wr, return_value);
}

extern "C" uint32_t nds_aicpu_rdma_recv(const NdsTransportDescriptor *transport, uint32_t queue_index,
                                        const NdsRecvWr *wr, int32_t *return_value) {
    if (transport == nullptr || wr == nullptr || return_value == nullptr)
        return kNdsAicpuInvalidArgument;
    const NdsQpDescriptor *qp = nds_transport_qp(transport, queue_index);
    return qp == nullptr ? kNdsAicpuInvalidArgument : nds_aicpu_post_recv(qp, wr, return_value);
}

extern "C" uint32_t nds_aicpu_rdma_read(const NdsTransportDescriptor *transport, uint32_t queue_index,
                                        const NdsSendWr *wr, int32_t *return_value) {
    return post(transport, queue_index, wr, return_value);
}

extern "C" uint32_t nds_aicpu_rdma_write(const NdsTransportDescriptor *transport, uint32_t queue_index,
                                         const NdsSendWr *wr, int32_t *return_value) {
    return post(transport, queue_index, wr, return_value);
}
