#ifndef NDS_AICPU_DEVICE_API_H
#define NDS_AICPU_DEVICE_API_H

#include "device_transport.h"
#include "device_storage.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

uint32_t nds_aicpu_post_send(const NdsQpDescriptor *qp, const NdsSendWr *wr, int32_t *return_value);
uint32_t nds_aicpu_post_recv(const NdsQpDescriptor *qp, const NdsRecvWr *wr, int32_t *return_value);
uint32_t nds_aicpu_poll_cq(const NdsQpDescriptor *qp, uint32_t is_send_cq, uint32_t max_completions, NdsWc *wc,
                           int32_t *return_value);

uint32_t nds_aicpu_rdma_send(const NdsTransportDescriptor *transport, uint32_t queue_index, const NdsSendWr *wr,
                             int32_t *return_value);
uint32_t nds_aicpu_rdma_recv(const NdsTransportDescriptor *transport, uint32_t queue_index, const NdsRecvWr *wr,
                             int32_t *return_value);
uint32_t nds_aicpu_rdma_read(const NdsTransportDescriptor *transport, uint32_t queue_index, const NdsSendWr *wr,
                             int32_t *return_value);
uint32_t nds_aicpu_rdma_write(const NdsTransportDescriptor *transport, uint32_t queue_index, const NdsSendWr *wr,
                              int32_t *return_value);

uint32_t nds_aicpu_storage_read(const NdsStorageContext *context, const nds::StorageReadCommand *command,
                                int32_t *return_value);
uint32_t nds_aicpu_storage_write(const NdsStorageContext *context, const nds::StorageWriteCommand *command,
                                 int32_t *return_value);
uint32_t nds_aicpu_storage_batch_read(const NdsStorageContext *context, const nds::StorageBatchReadCommand *command,
                                      int32_t *return_value);
uint32_t nds_aicpu_storage_batch_write(const NdsStorageContext *context, const nds::StorageBatchWriteCommand *command,
                                       int32_t *return_value);
uint32_t nds_aicpu_storage_wait(const NdsStorageContext *context, uint64_t command_id, uint64_t expected_bytes,
                                int32_t *return_value);

#ifdef __cplusplus
}
#endif

#endif
