#include "api.h"
#include "internal.h"

NDS_AIV_DEVICE_API_LINKAGE __aicore__ void NdsAivRdmaSendImpl(__gm__ const NdsDeviceTransport *transport,
                                                              const NdsDeviceSendWr *wr,
                                                              TBuf<> *scratch,
                                                              __gm__ NdsDeviceOperationResult *result) {
    if (result == nullptr)
        return;
    if (transport == nullptr || wr == nullptr ||
        (transport->abi_version != NDS_DEVICE_TRANSPORT_ABI_VERSION || transport->size != sizeof(*transport))) {
        NdsAivSetResult(result, NDS_DEVICE_OPERATION_INVALID_ARGUMENT);
        return;
    }
    NdsAivPostSendImpl(&transport->control_qp, wr, scratch, result);
}

NDS_AIV_DEVICE_API_LINKAGE __aicore__ void NdsAivRdmaRecvImpl(__gm__ const NdsDeviceTransport *transport,
                                                              const NdsDeviceSendWr *wr,
                                                              TBuf<> *scratch,
                                                              __gm__ NdsDeviceOperationResult *result) {
    if (result == nullptr)
        return;
    if (transport == nullptr || wr == nullptr ||
        (transport->abi_version != NDS_DEVICE_TRANSPORT_ABI_VERSION || transport->size != sizeof(*transport))) {
        NdsAivSetResult(result, NDS_DEVICE_OPERATION_INVALID_ARGUMENT);
        return;
    }
    NdsDeviceRecvWr recv{};
    recv.wr_id = wr->wr_id;
    recv.local.address = wr->local.address;
    recv.local.length = wr->local.length;
    recv.local.local_key = wr->local.local_key;
    NdsAivPostRecvImpl(&transport->control_qp, &recv, scratch, result);
}

NDS_AIV_DEVICE_API_LINKAGE __aicore__ void NdsAivRdmaReadImpl(__gm__ const NdsDeviceTransport *transport,
                                                              const NdsDeviceSendWr *wr,
                                                              TBuf<> *scratch,
                                                              __gm__ NdsDeviceOperationResult *result) {
    if (result == nullptr)
        return;
    if (transport == nullptr || wr == nullptr ||
        (transport->abi_version != NDS_DEVICE_TRANSPORT_ABI_VERSION || transport->size != sizeof(*transport))) {
        NdsAivSetResult(result, NDS_DEVICE_OPERATION_INVALID_ARGUMENT);
        return;
    }
    NdsAivPostSendImpl(&transport->control_qp, wr, scratch, result);
}

NDS_AIV_DEVICE_API_LINKAGE __aicore__ void NdsAivRdmaWriteImpl(__gm__ const NdsDeviceTransport *transport,
                                                               const NdsDeviceSendWr *wr,
                                                               TBuf<> *scratch,
                                                               __gm__ NdsDeviceOperationResult *result) {
    if (result == nullptr)
        return;
    if (transport == nullptr || wr == nullptr ||
        (transport->abi_version != NDS_DEVICE_TRANSPORT_ABI_VERSION || transport->size != sizeof(*transport))) {
        NdsAivSetResult(result, NDS_DEVICE_OPERATION_INVALID_ARGUMENT);
        return;
    }
    NdsAivPostSendImpl(&transport->control_qp, wr, scratch, result);
}
