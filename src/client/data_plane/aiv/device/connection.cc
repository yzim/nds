#include "nds/aiv_device_api.h"
#include "nds/aiv_device_internal.h"

NDS_AIV_DEVICE_API_LINKAGE __aicore__ void NdsAivRdmaSend(__gm__ const nds_device_connection *connection,
                                           __gm__ const nds_device_transfer *transfer, TBuf<> *scratch,
                                           __gm__ nds_device_operation_result *result) {
    if (result == nullptr) return;
    if (connection == nullptr || transfer == nullptr ||
        (connection->abi_version != NDS_DEVICE_CONNECTION_ABI_VERSION ||
         connection->size != sizeof(*connection))) {
        NdsAivSetResult(result, NDS_DEVICE_OPERATION_INVALID_ARGUMENT);
        return;
    }
    nds_device_send_wr wr{};
    wr.wr_id = transfer->wr_id;
    wr.opcode = NDS_DEVICE_WR_SEND;
    wr.flags = NDS_DEVICE_SEND_SIGNALED;
    wr.local.address = transfer->local.address;
    wr.local.length = transfer->local.length;
    wr.local.local_key = transfer->local.local_key;
    NdsAivPostSend(&connection->qp, &wr, scratch, result);
}

NDS_AIV_DEVICE_API_LINKAGE __aicore__ void NdsAivRdmaRecv(__gm__ const nds_device_connection *connection,
                                           __gm__ const nds_device_transfer *transfer, TBuf<> *scratch,
                                           __gm__ nds_device_operation_result *result) {
    if (result == nullptr) return;
    if (connection == nullptr || transfer == nullptr ||
        (connection->abi_version != NDS_DEVICE_CONNECTION_ABI_VERSION ||
         connection->size != sizeof(*connection))) {
        NdsAivSetResult(result, NDS_DEVICE_OPERATION_INVALID_ARGUMENT);
        return;
    }
    nds_device_recv_wr wr{};
    wr.wr_id = transfer->wr_id;
    wr.local.address = transfer->local.address;
    wr.local.length = transfer->local.length;
    wr.local.local_key = transfer->local.local_key;
    NdsAivPostRecv(&connection->qp, &wr, scratch, result);
}

NDS_AIV_DEVICE_API_LINKAGE __aicore__ void NdsAivRdmaRead(__gm__ const nds_device_connection *connection,
                                           __gm__ const nds_device_transfer *transfer, TBuf<> *scratch,
                                           __gm__ nds_device_operation_result *result) {
    if (result == nullptr) return;
    if (connection == nullptr || transfer == nullptr ||
        (connection->abi_version != NDS_DEVICE_CONNECTION_ABI_VERSION ||
         connection->size != sizeof(*connection))) {
        NdsAivSetResult(result, NDS_DEVICE_OPERATION_INVALID_ARGUMENT);
        return;
    }
    nds_device_send_wr wr{};
    wr.wr_id = transfer->wr_id;
    wr.opcode = NDS_DEVICE_WR_RDMA_READ;
    wr.flags = NDS_DEVICE_SEND_SIGNALED;
    wr.local.address = transfer->local.address;
    wr.local.length = transfer->local.length;
    wr.local.local_key = transfer->local.local_key;
    wr.remote_address = transfer->remote_address;
    wr.remote_key = transfer->remote_key;
    NdsAivPostSend(&connection->qp, &wr, scratch, result);
}

NDS_AIV_DEVICE_API_LINKAGE __aicore__ void NdsAivRdmaWrite(__gm__ const nds_device_connection *connection,
                                            __gm__ const nds_device_transfer *transfer, TBuf<> *scratch,
                                            __gm__ nds_device_operation_result *result) {
    if (result == nullptr) return;
    if (connection == nullptr || transfer == nullptr ||
        (connection->abi_version != NDS_DEVICE_CONNECTION_ABI_VERSION ||
         connection->size != sizeof(*connection))) {
        NdsAivSetResult(result, NDS_DEVICE_OPERATION_INVALID_ARGUMENT);
        return;
    }
    nds_device_send_wr wr{};
    wr.wr_id = transfer->wr_id;
    wr.opcode = NDS_DEVICE_WR_RDMA_WRITE;
    wr.flags = NDS_DEVICE_SEND_SIGNALED;
    wr.local.address = transfer->local.address;
    wr.local.length = transfer->local.length;
    wr.local.local_key = transfer->local.local_key;
    wr.remote_address = transfer->remote_address;
    wr.remote_key = transfer->remote_key;
    NdsAivPostSend(&connection->qp, &wr, scratch, result);
}
