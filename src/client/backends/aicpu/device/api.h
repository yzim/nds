#ifndef NDS_AICPU_DEVICE_API_H
#define NDS_AICPU_DEVICE_API_H

#include "backend_transport.h"
#include "backend_storage.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

uint32_t nds_aicpu_post_send(const NdsQpDescriptor *qp, const NdsSendWr *wr, int32_t *return_value);
uint32_t nds_aicpu_post_recv(const NdsQpDescriptor *qp, const NdsRecvWr *wr, int32_t *return_value);
uint32_t nds_aicpu_poll_cq(const NdsQpDescriptor *qp, uint32_t is_send_cq, uint32_t max_completions, NdsWc *wc,
                           int32_t *return_value);

/* Transport owns signaling through NdsTransportQpState. Callers, including
 * direct device-operator users, must leave NDS_SEND_SIGNALED clear. */
uint32_t nds_aicpu_rdma_send(const NdsTransportDescriptor *transport, uint32_t queue_index, const NdsSendWr *wr,
                             int32_t *return_value);
uint32_t nds_aicpu_rdma_recv(const NdsTransportDescriptor *transport, uint32_t queue_index, const NdsRecvWr *wr,
                             int32_t *return_value);
uint32_t nds_aicpu_rdma_read(const NdsTransportDescriptor *transport, uint32_t queue_index, const NdsSendWr *wr,
                             int32_t *return_value);
/* The transport applies its signal interval; leave NDS_SEND_SIGNALED clear. */
uint32_t nds_aicpu_rdma_write(const NdsTransportDescriptor *transport, uint32_t queue_index, const NdsSendWr *wr,
                              int32_t *return_value);

uint32_t nds_aicpu_storage_read(const NdsStorageDescriptor *descriptor, const nds::StorageReadCommand *command,
                                int32_t *return_value);
uint32_t nds_aicpu_storage_write(const NdsStorageDescriptor *descriptor, const nds::StorageWriteCommand *command,
                                 int32_t *return_value);
uint32_t nds_aicpu_storage_batch_read(const NdsStorageDescriptor *descriptor,
                                      const nds::StorageBatchReadCommand *command, int32_t *return_value);
uint32_t nds_aicpu_storage_batch_write(const NdsStorageDescriptor *descriptor,
                                       const nds::StorageBatchWriteCommand *command, int32_t *return_value);
uint32_t nds_aicpu_storage_wait(const NdsStorageDescriptor *descriptor, uint64_t command_id, uint64_t expected_bytes,
                                uint32_t slot_index, int32_t *return_value);

#ifdef __cplusplus
}
#endif

#endif
