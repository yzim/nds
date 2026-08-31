#include "api.h"
#include "internal.h"

NDS_AIV_DEVICE_API_LINKAGE __aicore__ void nds_aiv_rdma_send(__gm__ const NdsDeviceTransport *transport,
                                                             const NdsDeviceSendWr *wr, __gm__ int32_t *return_value,
                                                             TBuf<> *scratch) {
    if (return_value == nullptr)
        return;
    if (transport == nullptr || wr == nullptr) {
        NdsAivSetReturnValue(return_value, NDS_DEVICE_OPERATION_INVALID_ARGUMENT);
        return;
    }
    const NdsDeviceQp *qp = nds_device_transport_qp(transport, 0U);
    if (qp == nullptr) {
        NdsAivSetReturnValue(return_value, NDS_DEVICE_OPERATION_INVALID_ARGUMENT);
        return;
    }
    nds_aiv_post_send(qp, wr, return_value, scratch);
}

NDS_AIV_DEVICE_API_LINKAGE __aicore__ void nds_aiv_rdma_recv(__gm__ const NdsDeviceTransport *transport,
                                                             const NdsDeviceRecvWr *wr, __gm__ int32_t *return_value) {
    if (return_value == nullptr)
        return;
    if (transport == nullptr || wr == nullptr) {
        NdsAivSetReturnValue(return_value, NDS_DEVICE_OPERATION_INVALID_ARGUMENT);
        return;
    }
    const NdsDeviceQp *qp = nds_device_transport_qp(transport, 0U);
    if (qp == nullptr) {
        NdsAivSetReturnValue(return_value, NDS_DEVICE_OPERATION_INVALID_ARGUMENT);
        return;
    }
    nds_aiv_post_recv(qp, wr, return_value);
}

NDS_AIV_DEVICE_API_LINKAGE __aicore__ void nds_aiv_rdma_read(__gm__ const NdsDeviceTransport *transport,
                                                             const NdsDeviceSendWr *wr, __gm__ int32_t *return_value,
                                                             TBuf<> *scratch) {
    if (return_value == nullptr)
        return;
    if (transport == nullptr || wr == nullptr) {
        NdsAivSetReturnValue(return_value, NDS_DEVICE_OPERATION_INVALID_ARGUMENT);
        return;
    }
    const NdsDeviceQp *qp = nds_device_transport_qp(transport, 0U);
    if (qp == nullptr) {
        NdsAivSetReturnValue(return_value, NDS_DEVICE_OPERATION_INVALID_ARGUMENT);
        return;
    }
    nds_aiv_post_send(qp, wr, return_value, scratch);
}

NDS_AIV_DEVICE_API_LINKAGE __aicore__ void nds_aiv_rdma_write(__gm__ const NdsDeviceTransport *transport,
                                                              const NdsDeviceSendWr *wr, __gm__ int32_t *return_value,
                                                              TBuf<> *scratch) {
    if (return_value == nullptr)
        return;
    if (transport == nullptr || wr == nullptr) {
        NdsAivSetReturnValue(return_value, NDS_DEVICE_OPERATION_INVALID_ARGUMENT);
        return;
    }
    const NdsDeviceQp *qp = nds_device_transport_qp(transport, 0U);
    if (qp == nullptr) {
        NdsAivSetReturnValue(return_value, NDS_DEVICE_OPERATION_INVALID_ARGUMENT);
        return;
    }
    nds_aiv_post_send(qp, wr, return_value, scratch);
}
