#ifndef NDS_AIV_DEVICE_API_H
#define NDS_AIV_DEVICE_API_H

#include "kernel_operator.h"
#include "nds/device_operations.h"

#ifndef NDS_AIV_DEVICE_API_LINKAGE
#define NDS_AIV_DEVICE_API_LINKAGE extern "C"
#endif

NDS_AIV_DEVICE_API_LINKAGE __aicore__ void NdsAivPostSend(__gm__ const nds_device_qp *qp,
                               const nds_device_send_wr *wr,
                               AscendC::TBuf<> *scratch,
                               __gm__ nds_device_operation_result *result);
NDS_AIV_DEVICE_API_LINKAGE __aicore__ void NdsAivPostRecv(__gm__ const nds_device_qp *qp,
                               const nds_device_recv_wr *wr,
                               AscendC::TBuf<> *scratch,
                               __gm__ nds_device_operation_result *result);
NDS_AIV_DEVICE_API_LINKAGE __aicore__ void NdsAivPollCq(__gm__ const nds_device_qp *qp,
                             __gm__ const nds_device_poll_cq_request *request,
                             AscendC::TBuf<> *scratch,
                             __gm__ nds_device_operation_result *result);

NDS_AIV_DEVICE_API_LINKAGE __aicore__ void NdsAivRdmaSend(__gm__ const nds_device_connection *connection,
                               __gm__ const nds_device_transfer *transfer,
                               AscendC::TBuf<> *scratch,
                               __gm__ nds_device_operation_result *result);
NDS_AIV_DEVICE_API_LINKAGE __aicore__ void NdsAivRdmaRecv(__gm__ const nds_device_connection *connection,
                               __gm__ const nds_device_transfer *transfer,
                               AscendC::TBuf<> *scratch,
                               __gm__ nds_device_operation_result *result);
NDS_AIV_DEVICE_API_LINKAGE __aicore__ void NdsAivRdmaRead(__gm__ const nds_device_connection *connection,
                               __gm__ const nds_device_transfer *transfer,
                               AscendC::TBuf<> *scratch,
                               __gm__ nds_device_operation_result *result);
NDS_AIV_DEVICE_API_LINKAGE __aicore__ void NdsAivRdmaWrite(__gm__ const nds_device_connection *connection,
                                __gm__ const nds_device_transfer *transfer,
                                AscendC::TBuf<> *scratch,
                                __gm__ nds_device_operation_result *result);

#endif
