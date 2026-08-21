#include "api.h"
#include "internal.h"

NDS_AIV_DEVICE_API_LINKAGE __aicore__ void NdsAivRdmaSendImpl(__gm__ const NdsDeviceTransport *transport,
                                                              const NdsDeviceSendWr *wr,
                                                              __gm__ int32_t *return_value,
                                                              TBuf<> *scratch) {
    if (return_value == nullptr)
        return;
    if (transport == nullptr || wr == nullptr) {
        NdsAivSetReturnValue(return_value, NDS_DEVICE_OPERATION_INVALID_ARGUMENT);
        return;
    }
    NdsAivPostSendImpl(&transport->control_qp, wr, return_value, scratch);
}

NDS_AIV_DEVICE_API_LINKAGE __aicore__ void NdsAivRdmaRecvImpl(__gm__ const NdsDeviceTransport *transport,
                                                              const NdsDeviceSendWr *wr,
                                                              __gm__ int32_t *return_value,
                                                              TBuf<> *scratch) {
    if (return_value == nullptr)
        return;
    if (transport == nullptr || wr == nullptr) {
        NdsAivSetReturnValue(return_value, NDS_DEVICE_OPERATION_INVALID_ARGUMENT);
        return;
    }
    NdsDeviceRecvWr recv{};
    recv.wr_id = wr->wr_id;
    recv.local.address = wr->local.address;
    recv.local.length = wr->local.length;
    recv.local.local_key = wr->local.local_key;
    NdsAivPostRecvImpl(&transport->control_qp, &recv, return_value, scratch);
}

NDS_AIV_DEVICE_API_LINKAGE __aicore__ void NdsAivRdmaReadImpl(__gm__ const NdsDeviceTransport *transport,
                                                              const NdsDeviceSendWr *wr,
                                                              __gm__ int32_t *return_value,
                                                              TBuf<> *scratch) {
    if (return_value == nullptr)
        return;
    if (transport == nullptr || wr == nullptr) {
        NdsAivSetReturnValue(return_value, NDS_DEVICE_OPERATION_INVALID_ARGUMENT);
        return;
    }
    NdsAivPostSendImpl(&transport->control_qp, wr, return_value, scratch);
}

NDS_AIV_DEVICE_API_LINKAGE __aicore__ void NdsAivRdmaWriteImpl(__gm__ const NdsDeviceTransport *transport,
                                                               const NdsDeviceSendWr *wr,
                                                               __gm__ int32_t *return_value,
                                                               TBuf<> *scratch) {
    if (return_value == nullptr)
        return;
    if (transport == nullptr || wr == nullptr) {
        NdsAivSetReturnValue(return_value, NDS_DEVICE_OPERATION_INVALID_ARGUMENT);
        return;
    }
    NdsAivPostSendImpl(&transport->control_qp, wr, return_value, scratch);
}
