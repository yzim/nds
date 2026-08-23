#ifndef NDS_AIV_DEVICE_API_H
#define NDS_AIV_DEVICE_API_H

#include "kernel_operator.h"
#include "nds/device_transport.h"
#include "nds/device_storage.h"
#include "nds/device_benchmark.h"

#ifndef NDS_AIV_DEVICE_API_LINKAGE
#define NDS_AIV_DEVICE_API_LINKAGE extern "C"
#endif

NDS_AIV_DEVICE_API_LINKAGE __aicore__ void NdsAivPostSendImpl(__gm__ const NdsDeviceQp *qp,
                                                              const NdsDeviceSendWr *wr,
                                                              __gm__ int32_t *return_value,
                                                              AscendC::TBuf<> *scratch);
NDS_AIV_DEVICE_API_LINKAGE __aicore__ void NdsAivPostRecvImpl(__gm__ const NdsDeviceQp *qp,
                                                              const NdsDeviceRecvWr *wr,
                                                              __gm__ int32_t *return_value,
                                                              AscendC::TBuf<> *scratch);
NDS_AIV_DEVICE_API_LINKAGE __aicore__ void NdsAivPollCqImpl(
    __gm__ const NdsDeviceQp *qp, uint32_t is_send_cq, uint32_t max_completions,
    __gm__ NdsDeviceWc *wc, __gm__ int32_t *return_value, AscendC::TBuf<> *scratch);

NDS_AIV_DEVICE_API_LINKAGE __aicore__ void NdsAivRdmaSendImpl(__gm__ const NdsDeviceTransport *transport,
                                                              const NdsDeviceSendWr *wr,
                                                              __gm__ int32_t *return_value,
                                                              AscendC::TBuf<> *scratch);
NDS_AIV_DEVICE_API_LINKAGE __aicore__ void NdsAivRdmaRecvImpl(__gm__ const NdsDeviceTransport *transport,
                                                              const NdsDeviceRecvWr *wr,
                                                              __gm__ int32_t *return_value,
                                                              AscendC::TBuf<> *scratch);
NDS_AIV_DEVICE_API_LINKAGE __aicore__ void NdsAivRdmaReadImpl(__gm__ const NdsDeviceTransport *transport,
                                                              const NdsDeviceSendWr *wr,
                                                              __gm__ int32_t *return_value,
                                                              AscendC::TBuf<> *scratch);
NDS_AIV_DEVICE_API_LINKAGE __aicore__ void NdsAivRdmaWriteImpl(__gm__ const NdsDeviceTransport *transport,
                                                               const NdsDeviceSendWr *wr,
                                                               __gm__ int32_t *return_value,
                                                               AscendC::TBuf<> *scratch);
NDS_AIV_DEVICE_API_LINKAGE __aicore__ void NdsAivRdmaBenchmarkImpl(
    __gm__ NdsDeviceRdmaBenchmarkArgs *args, AscendC::TBuf<> *scratch);

NDS_AIV_DEVICE_API_LINKAGE __aicore__ void NdsAivStorageReadImpl(__gm__ const NdsDeviceStorageContext *context,
                                                                 __gm__ const nds::StorageReadCommand *command,
                                                                 __gm__ int32_t *return_value,
                                                                 AscendC::TBuf<> *scratch);
NDS_AIV_DEVICE_API_LINKAGE __aicore__ void NdsAivStorageWriteImpl(__gm__ const NdsDeviceStorageContext *context,
                                                                  __gm__ const nds::StorageWriteCommand *command,
                                                                  __gm__ int32_t *return_value,
                                                                  AscendC::TBuf<> *scratch);
NDS_AIV_DEVICE_API_LINKAGE __aicore__ void NdsAivStorageBatchReadImpl(
    __gm__ const NdsDeviceStorageContext *context, __gm__ const nds::StorageBatchReadCommand *command,
    __gm__ int32_t *return_value, AscendC::TBuf<> *scratch);
NDS_AIV_DEVICE_API_LINKAGE __aicore__ void NdsAivStorageBatchWriteImpl(
    __gm__ const NdsDeviceStorageContext *context, __gm__ const nds::StorageBatchWriteCommand *command,
    __gm__ int32_t *return_value, AscendC::TBuf<> *scratch);
NDS_AIV_DEVICE_API_LINKAGE __aicore__ void NdsAivStorageWaitImpl(__gm__ const NdsDeviceStorageContext *context,
                                                                 uint64_t command_id, uint64_t expected_bytes,
                                                                 __gm__ int32_t *return_value);

#endif
