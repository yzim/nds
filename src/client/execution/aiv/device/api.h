#ifndef NDS_AIV_DEVICE_API_H
#define NDS_AIV_DEVICE_API_H

#include "kernel_operator.h"
#include "nds/device_operations.h"
#include "nds/device_storage.h"

#ifndef NDS_AIV_DEVICE_API_LINKAGE
#define NDS_AIV_DEVICE_API_LINKAGE extern "C"
#endif

NDS_AIV_DEVICE_API_LINKAGE __aicore__ void NdsAivPostSendImpl(__gm__ const nds_device_qp *qp,
                               const nds_device_send_wr *wr,
                               AscendC::TBuf<> *scratch,
                               __gm__ nds_device_operation_result *result);
NDS_AIV_DEVICE_API_LINKAGE __aicore__ void NdsAivPostRecvImpl(__gm__ const nds_device_qp *qp,
                               const nds_device_recv_wr *wr,
                               AscendC::TBuf<> *scratch,
                               __gm__ nds_device_operation_result *result);
NDS_AIV_DEVICE_API_LINKAGE __aicore__ void NdsAivPollCqImpl(__gm__ const nds_device_qp *qp,
                             __gm__ const nds_device_poll_cq_request *request,
                             AscendC::TBuf<> *scratch,
                             __gm__ nds_device_operation_result *result);

NDS_AIV_DEVICE_API_LINKAGE __aicore__ void NdsAivRdmaSendImpl(__gm__ const nds_device_connection *connection,
                               __gm__ const nds_device_transfer *transfer,
                               AscendC::TBuf<> *scratch,
                               __gm__ nds_device_operation_result *result);
NDS_AIV_DEVICE_API_LINKAGE __aicore__ void NdsAivRdmaRecvImpl(__gm__ const nds_device_connection *connection,
                               __gm__ const nds_device_transfer *transfer,
                               AscendC::TBuf<> *scratch,
                               __gm__ nds_device_operation_result *result);
NDS_AIV_DEVICE_API_LINKAGE __aicore__ void NdsAivRdmaReadImpl(__gm__ const nds_device_connection *connection,
                               __gm__ const nds_device_transfer *transfer,
                               AscendC::TBuf<> *scratch,
                               __gm__ nds_device_operation_result *result);
NDS_AIV_DEVICE_API_LINKAGE __aicore__ void NdsAivRdmaWriteImpl(__gm__ const nds_device_connection *connection,
                                __gm__ const nds_device_transfer *transfer,
                                AscendC::TBuf<> *scratch,
                                __gm__ nds_device_operation_result *result);

NDS_AIV_DEVICE_API_LINKAGE __aicore__ void NdsAivStorageReadImpl(__gm__ const nds_device_storage *storage,
                                 __gm__ const nds_device_storage_io *io,
                                 AscendC::TBuf<> *scratch,
                                 __gm__ nds_device_operation_result *result);
NDS_AIV_DEVICE_API_LINKAGE __aicore__ void NdsAivStorageWriteImpl(__gm__ const nds_device_storage *storage,
                                  __gm__ const nds_device_storage_io *io,
                                  AscendC::TBuf<> *scratch,
                                  __gm__ nds_device_operation_result *result);

#endif
