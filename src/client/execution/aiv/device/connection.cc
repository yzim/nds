#include "api.h"
#include "internal.h"

NDS_AIV_DEVICE_API_LINKAGE __aicore__ void NdsAivRdmaSendImpl(__gm__ const NdsDeviceTransport *transport,
                                                              const NdsDeviceTransfer *transfer,
                                                              TBuf<> *scratch,
                                                              __gm__ NdsDeviceOperationResult *result) {
    if (result == nullptr)
        return;
    if (transport == nullptr || transfer == nullptr ||
        (transport->abi_version != NDS_DEVICE_TRANSPORT_ABI_VERSION || transport->size != sizeof(*transport))) {
        NdsAivSetResult(result, NDS_DEVICE_OPERATION_INVALID_ARGUMENT);
        return;
    }
    NdsDeviceSendWr wr{};
    wr.wr_id = transfer->wr_id;
    wr.opcode = NDS_DEVICE_WR_SEND;
    wr.flags = NDS_DEVICE_SEND_SIGNALED;
    wr.local.address = transfer->local.address;
    wr.local.length = transfer->local.length;
    wr.local.local_key = transfer->local.local_key;
    NdsAivPostSendImpl(&transport->control_qp, &wr, scratch, result);
}

NDS_AIV_DEVICE_API_LINKAGE __aicore__ void NdsAivRdmaRecvImpl(__gm__ const NdsDeviceTransport *transport,
                                                              __gm__ const NdsDeviceTransfer *transfer,
                                                              TBuf<> *scratch,
                                                              __gm__ NdsDeviceOperationResult *result) {
    if (result == nullptr)
        return;
    if (transport == nullptr || transfer == nullptr ||
        (transport->abi_version != NDS_DEVICE_TRANSPORT_ABI_VERSION || transport->size != sizeof(*transport))) {
        NdsAivSetResult(result, NDS_DEVICE_OPERATION_INVALID_ARGUMENT);
        return;
    }
    NdsDeviceRecvWr wr{};
    wr.wr_id = transfer->wr_id;
    wr.local.address = transfer->local.address;
    wr.local.length = transfer->local.length;
    wr.local.local_key = transfer->local.local_key;
    NdsAivPostRecvImpl(&transport->control_qp, &wr, scratch, result);
}

NDS_AIV_DEVICE_API_LINKAGE __aicore__ void NdsAivRdmaReadImpl(__gm__ const NdsDeviceTransport *transport,
                                                              __gm__ const NdsDeviceTransfer *transfer,
                                                              TBuf<> *scratch,
                                                              __gm__ NdsDeviceOperationResult *result) {
    if (result == nullptr)
        return;
    if (transport == nullptr || transfer == nullptr ||
        (transport->abi_version != NDS_DEVICE_TRANSPORT_ABI_VERSION || transport->size != sizeof(*transport))) {
        NdsAivSetResult(result, NDS_DEVICE_OPERATION_INVALID_ARGUMENT);
        return;
    }
    NdsDeviceSendWr wr{};
    wr.wr_id = transfer->wr_id;
    wr.opcode = NDS_DEVICE_WR_RDMA_READ;
    wr.flags = NDS_DEVICE_SEND_SIGNALED;
    wr.local.address = transfer->local.address;
    wr.local.length = transfer->local.length;
    wr.local.local_key = transfer->local.local_key;
    wr.remote_address = transfer->remote_address;
    wr.remote_key = transfer->remote_key;
    NdsAivPostSendImpl(&transport->control_qp, &wr, scratch, result);
}

NDS_AIV_DEVICE_API_LINKAGE __aicore__ void NdsAivRdmaWriteImpl(__gm__ const NdsDeviceTransport *transport,
                                                               __gm__ const NdsDeviceTransfer *transfer,
                                                               TBuf<> *scratch,
                                                               __gm__ NdsDeviceOperationResult *result) {
    if (result == nullptr)
        return;
    if (transport == nullptr || transfer == nullptr ||
        (transport->abi_version != NDS_DEVICE_TRANSPORT_ABI_VERSION || transport->size != sizeof(*transport))) {
        NdsAivSetResult(result, NDS_DEVICE_OPERATION_INVALID_ARGUMENT);
        return;
    }
    NdsDeviceSendWr wr{};
    wr.wr_id = transfer->wr_id;
    wr.opcode = NDS_DEVICE_WR_RDMA_WRITE;
    wr.flags = NDS_DEVICE_SEND_SIGNALED;
    wr.local.address = transfer->local.address;
    wr.local.length = transfer->local.length;
    wr.local.local_key = transfer->local.local_key;
    wr.remote_address = transfer->remote_address;
    wr.remote_key = transfer->remote_key;
    NdsAivPostSendImpl(&transport->control_qp, &wr, scratch, result);
}
