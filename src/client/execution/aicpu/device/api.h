#ifndef NDS_AICPU_DEVICE_API_H
#define NDS_AICPU_DEVICE_API_H

#include "nds/device_operations.h"
#include "nds/device_storage.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

uint32_t NdsAicpuPostSendImpl(
    const nds_device_qp *qp, const nds_device_send_wr *wr,
    nds_device_operation_result *result);
uint32_t NdsAicpuPostRecvImpl(
    const nds_device_qp *qp, const nds_device_recv_wr *wr,
    nds_device_operation_result *result);
uint32_t NdsAicpuPollCqImpl(
    const nds_device_qp *qp, const nds_device_poll_cq_request *request,
    nds_device_operation_result *result);

uint32_t NdsAicpuRdmaSendImpl(
    const nds_device_connection *connection, const nds_device_transfer *transfer,
    nds_device_operation_result *result);
uint32_t NdsAicpuRdmaRecvImpl(
    const nds_device_connection *connection, const nds_device_transfer *transfer,
    nds_device_operation_result *result);
uint32_t NdsAicpuRdmaReadImpl(
    const nds_device_connection *connection, const nds_device_transfer *transfer,
    nds_device_operation_result *result);
uint32_t NdsAicpuRdmaWriteImpl(
    const nds_device_connection *connection, const nds_device_transfer *transfer,
    nds_device_operation_result *result);

uint32_t NdsAicpuStorageReadImpl(
    const nds_device_storage *storage, const nds_device_storage_io *io,
    nds_device_operation_result *result);
uint32_t NdsAicpuStorageWriteImpl(
    const nds_device_storage *storage, const nds_device_storage_io *io,
    nds_device_operation_result *result);

#ifdef __cplusplus
}
#endif

#endif
