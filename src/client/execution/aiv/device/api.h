#ifndef NDS_AIV_DEVICE_API_H
#define NDS_AIV_DEVICE_API_H

#include "kernel_operator.h"
#include "nds/device_operations.h"
#include "nds/device_storage.h"

#ifndef NDS_AIV_DEVICE_API_LINKAGE
#define NDS_AIV_DEVICE_API_LINKAGE extern "C"
#endif

NDS_AIV_DEVICE_API_LINKAGE __aicore__ void NdsAivPostSendImpl(__gm__ const NdsDeviceQp *qp,
                                                              const NdsDeviceSendWr *wr, AscendC::TBuf<> *scratch,
                                                              __gm__ NdsDeviceOperationResult *result);
NDS_AIV_DEVICE_API_LINKAGE __aicore__ void NdsAivPostRecvImpl(__gm__ const NdsDeviceQp *qp,
                                                              const NdsDeviceRecvWr *wr, AscendC::TBuf<> *scratch,
                                                              __gm__ NdsDeviceOperationResult *result);
NDS_AIV_DEVICE_API_LINKAGE __aicore__ void NdsAivPollCqImpl(
    __gm__ const NdsDeviceQp *qp, __gm__ const NdsDevicePollCqRequest *request,
    AscendC::TBuf<> *scratch, __gm__ NdsDeviceOperationResult *result);

NDS_AIV_DEVICE_API_LINKAGE __aicore__ void NdsAivRdmaSendImpl(__gm__ const NdsDeviceTransport *transport,
                                                              const NdsDeviceTransfer *transfer,
                                                              AscendC::TBuf<> *scratch,
                                                              __gm__ NdsDeviceOperationResult *result);
NDS_AIV_DEVICE_API_LINKAGE __aicore__ void NdsAivRdmaRecvImpl(__gm__ const NdsDeviceTransport *transport,
                                                              __gm__ const NdsDeviceTransfer *transfer,
                                                              AscendC::TBuf<> *scratch,
                                                              __gm__ NdsDeviceOperationResult *result);
NDS_AIV_DEVICE_API_LINKAGE __aicore__ void NdsAivRdmaReadImpl(__gm__ const NdsDeviceTransport *transport,
                                                              __gm__ const NdsDeviceTransfer *transfer,
                                                              AscendC::TBuf<> *scratch,
                                                              __gm__ NdsDeviceOperationResult *result);
NDS_AIV_DEVICE_API_LINKAGE __aicore__ void NdsAivRdmaWriteImpl(__gm__ const NdsDeviceTransport *transport,
                                                               __gm__ const NdsDeviceTransfer *transfer,
                                                               AscendC::TBuf<> *scratch,
                                                               __gm__ NdsDeviceOperationResult *result);

NDS_AIV_DEVICE_API_LINKAGE __aicore__ void NdsAivStorageReadImpl(__gm__ const NdsDeviceStorageContext *context,
                                                                 __gm__ const nds::StorageReadCommand *command,
                                                                 AscendC::TBuf<> *scratch,
                                                                 __gm__ NdsDeviceOperationResult *result);
NDS_AIV_DEVICE_API_LINKAGE __aicore__ void NdsAivStorageWriteImpl(__gm__ const NdsDeviceStorageContext *context,
                                                                  __gm__ const nds::StorageWriteCommand *command,
                                                                  AscendC::TBuf<> *scratch,
                                                                  __gm__ NdsDeviceOperationResult *result);
NDS_AIV_DEVICE_API_LINKAGE __aicore__ void NdsAivStorageBatchReadImpl(
    __gm__ const NdsDeviceStorageContext *context, __gm__ const nds::StorageBatchReadCommand *command,
    AscendC::TBuf<> *scratch, __gm__ NdsDeviceOperationResult *result);
NDS_AIV_DEVICE_API_LINKAGE __aicore__ void NdsAivStorageBatchWriteImpl(
    __gm__ const NdsDeviceStorageContext *context, __gm__ const nds::StorageBatchWriteCommand *command,
    AscendC::TBuf<> *scratch, __gm__ NdsDeviceOperationResult *result);
NDS_AIV_DEVICE_API_LINKAGE __aicore__ void NdsAivStorageWaitImpl(__gm__ const NdsDeviceStorageContext *context,
                                                                 uint64_t command_id, uint64_t expected_bytes,
                                                                 __gm__ NdsDeviceOperationResult *result);

#endif
