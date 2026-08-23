#ifndef NDS_AICPU_DEVICE_API_H
#define NDS_AICPU_DEVICE_API_H

#include "nds/device_transport.h"
#include "nds/device_benchmark.h"
#include "nds/device_storage.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

uint32_t NdsAicpuPostSendImpl(const NdsDeviceQp *qp, const NdsDeviceSendWr *wr,
                              int32_t *return_value, NdsDeviceDoorbell *doorbell);
uint32_t NdsAicpuPostSendListImpl(const NdsDeviceQp *qp, const NdsDeviceSendWr *wrs, uint32_t count,
                                  int32_t *return_value);
uint32_t NdsAicpuPostRecvImpl(const NdsDeviceQp *qp, const NdsDeviceRecvWr *wr,
                              int32_t *return_value);
uint32_t NdsAicpuPollCqImpl(const NdsDeviceQp *qp, uint32_t is_send_cq, uint32_t max_completions,
                            NdsDeviceWc *wc, int32_t *return_value);

uint32_t NdsAicpuRdmaSendImpl(const NdsDeviceTransport *transport, const NdsDeviceSendWr *wr,
                              int32_t *return_value);
uint32_t NdsAicpuRdmaRecvImpl(const NdsDeviceTransport *transport, const NdsDeviceRecvWr *wr,
                              int32_t *return_value);
uint32_t NdsAicpuRdmaReadImpl(const NdsDeviceTransport *transport, const NdsDeviceSendWr *wr,
                              int32_t *return_value);
uint32_t NdsAicpuRdmaWriteImpl(const NdsDeviceTransport *transport, const NdsDeviceSendWr *wr,
                               int32_t *return_value);
uint32_t NdsAicpuRdmaBenchmarkImpl(const NdsDeviceRdmaBenchmarkArgs *args);

uint32_t NdsAicpuStorageReadImpl(const NdsDeviceStorageContext *context, const nds::StorageReadCommand *command,
                                 int32_t *return_value);
uint32_t NdsAicpuStorageWriteImpl(const NdsDeviceStorageContext *context, const nds::StorageWriteCommand *command,
                                  int32_t *return_value);
uint32_t NdsAicpuStorageBatchReadImpl(const NdsDeviceStorageContext *context,
                                      const nds::StorageBatchReadCommand *command,
                                      int32_t *return_value);
uint32_t NdsAicpuStorageBatchWriteImpl(const NdsDeviceStorageContext *context,
                                       const nds::StorageBatchWriteCommand *command,
                                       int32_t *return_value);
uint32_t NdsAicpuStorageWaitImpl(const NdsDeviceStorageContext *context, uint64_t command_id,
                                 uint64_t expected_bytes, int32_t *return_value);

#ifdef __cplusplus
}
#endif

#endif
