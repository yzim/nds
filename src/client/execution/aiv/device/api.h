#ifndef NDS_AIV_DEVICE_API_H
#define NDS_AIV_DEVICE_API_H

#include "kernel_operator.h"
#include "nds/device_transport.h"
#include "nds/device_storage.h"

#ifndef NDS_AIV_DEVICE_API_LINKAGE
#define NDS_AIV_DEVICE_API_LINKAGE extern "C"
#endif

NDS_AIV_DEVICE_API_LINKAGE __aicore__ void nds_aiv_post_send(__gm__ const NdsDeviceQp *qp, const NdsDeviceSendWr *wr,
                                                             __gm__ int32_t *return_value, AscendC::TBuf<> *scratch);
/* Posts a contiguous WR array and returns the first unposted WR address on failure. */
NDS_AIV_DEVICE_API_LINKAGE __aicore__ void nds_aiv_post_send_batch(__gm__ const NdsDeviceQp *qp,
                                                                   __gm__ const NdsDeviceSendWr *wrs, uint32_t wr_count,
                                                                   __gm__ int32_t *return_value,
                                                                   __gm__ uint64_t *bad_wr, AscendC::TBuf<> *scratch);
NDS_AIV_DEVICE_API_LINKAGE __aicore__ void nds_aiv_post_recv(__gm__ const NdsDeviceQp *qp, const NdsDeviceRecvWr *wr,
                                                             __gm__ int32_t *return_value);
NDS_AIV_DEVICE_API_LINKAGE __aicore__ void nds_aiv_poll_cq(__gm__ const NdsDeviceQp *qp, uint32_t is_send_cq,
                                                           uint32_t max_completions, __gm__ NdsDeviceWc *wc,
                                                           __gm__ int32_t *return_value);

NDS_AIV_DEVICE_API_LINKAGE __aicore__ void nds_aiv_rdma_send(__gm__ const NdsDeviceTransport *transport,
                                                             const NdsDeviceSendWr *wr, __gm__ int32_t *return_value,
                                                             AscendC::TBuf<> *scratch);
NDS_AIV_DEVICE_API_LINKAGE __aicore__ void nds_aiv_rdma_recv(__gm__ const NdsDeviceTransport *transport,
                                                             const NdsDeviceRecvWr *wr, __gm__ int32_t *return_value);
NDS_AIV_DEVICE_API_LINKAGE __aicore__ void nds_aiv_rdma_read(__gm__ const NdsDeviceTransport *transport,
                                                             const NdsDeviceSendWr *wr, __gm__ int32_t *return_value,
                                                             AscendC::TBuf<> *scratch);
NDS_AIV_DEVICE_API_LINKAGE __aicore__ void nds_aiv_rdma_write(__gm__ const NdsDeviceTransport *transport,
                                                              const NdsDeviceSendWr *wr, __gm__ int32_t *return_value,
                                                              AscendC::TBuf<> *scratch);

NDS_AIV_DEVICE_API_LINKAGE __aicore__ void nds_aiv_storage_read(__gm__ const NdsDeviceStorageContext *context,
                                                                __gm__ const nds::StorageReadCommand *command,
                                                                __gm__ int32_t *return_value, AscendC::TBuf<> *scratch);
NDS_AIV_DEVICE_API_LINKAGE __aicore__ void nds_aiv_storage_write(__gm__ const NdsDeviceStorageContext *context,
                                                                 __gm__ const nds::StorageWriteCommand *command,
                                                                 __gm__ int32_t *return_value,
                                                                 AscendC::TBuf<> *scratch);
NDS_AIV_DEVICE_API_LINKAGE __aicore__ void nds_aiv_storage_batch_read(
    __gm__ const NdsDeviceStorageContext *context, __gm__ const nds::StorageBatchReadCommand *command,
    __gm__ int32_t *return_value, AscendC::TBuf<> *scratch);
NDS_AIV_DEVICE_API_LINKAGE __aicore__ void nds_aiv_storage_batch_write(
    __gm__ const NdsDeviceStorageContext *context, __gm__ const nds::StorageBatchWriteCommand *command,
    __gm__ int32_t *return_value, AscendC::TBuf<> *scratch);
NDS_AIV_DEVICE_API_LINKAGE __aicore__ void nds_aiv_storage_wait(__gm__ const NdsDeviceStorageContext *context,
                                                                uint64_t command_id, uint64_t expected_bytes,
                                                                __gm__ int32_t *return_value);

#endif
