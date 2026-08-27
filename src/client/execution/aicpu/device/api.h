#ifndef NDS_AICPU_DEVICE_API_H
#define NDS_AICPU_DEVICE_API_H

#include "nds/device_transport.h"
#include "nds/device_storage.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

uint32_t nds_aicpu_post_send(const NdsDeviceQp *qp, const NdsDeviceSendWr *wr, int32_t *return_value);
uint32_t nds_aicpu_post_recv(const NdsDeviceQp *qp, const NdsDeviceRecvWr *wr, int32_t *return_value);
uint32_t nds_aicpu_poll_cq(const NdsDeviceQp *qp, uint32_t is_send_cq, uint32_t max_completions, NdsDeviceWc *wc,
                           int32_t *return_value);

uint32_t nds_aicpu_rdma_send(const NdsDeviceTransport *transport, const NdsDeviceSendWr *wr, int32_t *return_value);
uint32_t nds_aicpu_rdma_recv(const NdsDeviceTransport *transport, const NdsDeviceRecvWr *wr, int32_t *return_value);
uint32_t nds_aicpu_rdma_read(const NdsDeviceTransport *transport, const NdsDeviceSendWr *wr, int32_t *return_value);
uint32_t nds_aicpu_rdma_write(const NdsDeviceTransport *transport, const NdsDeviceSendWr *wr, int32_t *return_value);

uint32_t nds_aicpu_storage_read(const NdsDeviceStorageContext *context, const nds::StorageReadCommand *command,
                                int32_t *return_value);
uint32_t nds_aicpu_storage_write(const NdsDeviceStorageContext *context, const nds::StorageWriteCommand *command,
                                 int32_t *return_value);
uint32_t nds_aicpu_storage_batch_read(const NdsDeviceStorageContext *context,
                                      const nds::StorageBatchReadCommand *command, int32_t *return_value);
uint32_t nds_aicpu_storage_batch_write(const NdsDeviceStorageContext *context,
                                       const nds::StorageBatchWriteCommand *command, int32_t *return_value);
uint32_t nds_aicpu_storage_wait(const NdsDeviceStorageContext *context, uint64_t command_id, uint64_t expected_bytes,
                                int32_t *return_value);

#ifdef __cplusplus
}
#endif

#endif
