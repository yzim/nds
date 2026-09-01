#include "api.h"
#include "internal.h"

NDS_AIV_DEVICE_API_LINKAGE __aicore__ void nds_aiv_rdma_send(__gm__ const NdsTransportDescriptor *transport,
                                                             uint32_t queue_index, const NdsSendWr *wr,
                                                             __gm__ int32_t *return_value, TBuf<> *scratch) {
    if (return_value == nullptr)
        return;
    if (transport == nullptr || wr == nullptr) {
        NdsAivSetReturnValue(return_value, NDS_OPERATION_INVALID_ARGUMENT);
        return;
    }
    const NdsQpDescriptor *qp = nds_transport_qp(transport, queue_index);
    if (qp == nullptr) {
        NdsAivSetReturnValue(return_value, NDS_OPERATION_INVALID_ARGUMENT);
        return;
    }
    nds_aiv_post_send(qp, wr, return_value, scratch);
}

NDS_AIV_DEVICE_API_LINKAGE __aicore__ void nds_aiv_rdma_send_batch(__gm__ const NdsTransportDescriptor *transport,
                                                                   uint32_t queue_index, __gm__ const NdsSendWr *wrs,
                                                                   uint32_t wr_count, __gm__ int32_t *return_value,
                                                                   __gm__ uint64_t *bad_wr, TBuf<> *scratch) {
    if (return_value == nullptr)
        return;
    if (transport == nullptr || wrs == nullptr || wr_count == 0U || bad_wr == nullptr) {
        NdsAivSetReturnValue(return_value, NDS_OPERATION_INVALID_ARGUMENT);
        return;
    }
    __gm__ const NdsQpDescriptor *qp = nds_transport_qp_global(transport, queue_index);
    if (qp == nullptr) {
        NdsAivSetReturnValue(return_value, NDS_OPERATION_INVALID_ARGUMENT);
        return;
    }
    nds_aiv_post_send_batch(qp, wrs, wr_count, return_value, bad_wr, scratch);
}

NDS_AIV_DEVICE_API_LINKAGE __aicore__ void nds_aiv_rdma_recv(__gm__ const NdsTransportDescriptor *transport,
                                                             uint32_t queue_index, const NdsRecvWr *wr,
                                                             __gm__ int32_t *return_value) {
    if (return_value == nullptr)
        return;
    if (transport == nullptr || wr == nullptr) {
        NdsAivSetReturnValue(return_value, NDS_OPERATION_INVALID_ARGUMENT);
        return;
    }
    const NdsQpDescriptor *qp = nds_transport_qp(transport, queue_index);
    if (qp == nullptr) {
        NdsAivSetReturnValue(return_value, NDS_OPERATION_INVALID_ARGUMENT);
        return;
    }
    nds_aiv_post_recv(qp, wr, return_value);
}

NDS_AIV_DEVICE_API_LINKAGE __aicore__ void nds_aiv_rdma_read(__gm__ const NdsTransportDescriptor *transport,
                                                             uint32_t queue_index, const NdsSendWr *wr,
                                                             __gm__ int32_t *return_value, TBuf<> *scratch) {
    if (return_value == nullptr)
        return;
    if (transport == nullptr || wr == nullptr) {
        NdsAivSetReturnValue(return_value, NDS_OPERATION_INVALID_ARGUMENT);
        return;
    }
    const NdsQpDescriptor *qp = nds_transport_qp(transport, queue_index);
    if (qp == nullptr) {
        NdsAivSetReturnValue(return_value, NDS_OPERATION_INVALID_ARGUMENT);
        return;
    }
    nds_aiv_post_send(qp, wr, return_value, scratch);
}

NDS_AIV_DEVICE_API_LINKAGE __aicore__ void nds_aiv_rdma_write(__gm__ const NdsTransportDescriptor *transport,
                                                              uint32_t queue_index, const NdsSendWr *wr,
                                                              __gm__ int32_t *return_value, TBuf<> *scratch) {
    if (return_value == nullptr)
        return;
    if (transport == nullptr || wr == nullptr) {
        NdsAivSetReturnValue(return_value, NDS_OPERATION_INVALID_ARGUMENT);
        return;
    }
    const NdsQpDescriptor *qp = nds_transport_qp(transport, queue_index);
    if (qp == nullptr) {
        NdsAivSetReturnValue(return_value, NDS_OPERATION_INVALID_ARGUMENT);
        return;
    }
    nds_aiv_post_send(qp, wr, return_value, scratch);
}
