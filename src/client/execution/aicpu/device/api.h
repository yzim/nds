#ifndef NDS_AICPU_DEVICE_API_H
#define NDS_AICPU_DEVICE_API_H

#include "nds/device_operations.h"
#include "nds/device_storage.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

uint32_t NdsAicpuPostSendImpl(const NdsDeviceQp *qp, const NdsDeviceSendWr *wr,
                              NdsDeviceOperationResult *result);
uint32_t NdsAicpuPostRecvImpl(const NdsDeviceQp *qp, const NdsDeviceRecvWr *wr,
                              NdsDeviceOperationResult *result);
uint32_t NdsAicpuPollCqImpl(const NdsDeviceQp *qp, const NdsDevicePollCqRequest *request,
                            NdsDeviceOperationResult *result);

uint32_t NdsAicpuRdmaSendImpl(const NdsDeviceTransport *transport, const NdsDeviceSendWr *wr,
                              NdsDeviceOperationResult *result);
uint32_t NdsAicpuRdmaRecvImpl(const NdsDeviceTransport *transport, const NdsDeviceSendWr *wr,
                              NdsDeviceOperationResult *result);
uint32_t NdsAicpuRdmaReadImpl(const NdsDeviceTransport *transport, const NdsDeviceSendWr *wr,
                              NdsDeviceOperationResult *result);
uint32_t NdsAicpuRdmaWriteImpl(const NdsDeviceTransport *transport, const NdsDeviceSendWr *wr,
                               NdsDeviceOperationResult *result);

uint32_t NdsAicpuStorageReadImpl(const NdsDeviceStorageContext *context, const nds::StorageReadCommand *command,
                                 NdsDeviceOperationResult *result);
uint32_t NdsAicpuStorageWriteImpl(const NdsDeviceStorageContext *context, const nds::StorageWriteCommand *command,
                                  NdsDeviceOperationResult *result);
uint32_t NdsAicpuStorageBatchReadImpl(const NdsDeviceStorageContext *context,
                                      const nds::StorageBatchReadCommand *command,
                                      NdsDeviceOperationResult *result);
uint32_t NdsAicpuStorageBatchWriteImpl(const NdsDeviceStorageContext *context,
                                       const nds::StorageBatchWriteCommand *command,
                                       NdsDeviceOperationResult *result);
uint32_t NdsAicpuStorageWaitImpl(const NdsDeviceStorageContext *context, uint64_t command_id,
                                 uint64_t expected_bytes, NdsDeviceOperationResult *result);

#ifdef __cplusplus
}
#endif

#endif
