#ifndef NDS_AICPU_DEVICE_API_H
#define NDS_AICPU_DEVICE_API_H

#include "nds/device_operations.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

__attribute__((visibility("default"))) uint32_t NdsAicpuPostSend(
    const nds_device_qp *qp, const nds_device_send_wr *wr,
    nds_device_operation_result *result);
__attribute__((visibility("default"))) uint32_t NdsAicpuPostRecv(
    const nds_device_qp *qp, const nds_device_recv_wr *wr,
    nds_device_operation_result *result);
__attribute__((visibility("default"))) uint32_t NdsAicpuPollCq(
    const nds_device_qp *qp, const nds_device_poll_cq_request *request,
    nds_device_operation_result *result);

__attribute__((visibility("default"))) uint32_t NdsAicpuRdmaSend(
    const nds_device_connection *connection, const nds_device_transfer *transfer,
    nds_device_operation_result *result);
__attribute__((visibility("default"))) uint32_t NdsAicpuRdmaRecv(
    const nds_device_connection *connection, const nds_device_transfer *transfer,
    nds_device_operation_result *result);
__attribute__((visibility("default"))) uint32_t NdsAicpuRdmaRead(
    const nds_device_connection *connection, const nds_device_transfer *transfer,
    nds_device_operation_result *result);
__attribute__((visibility("default"))) uint32_t NdsAicpuRdmaWrite(
    const nds_device_connection *connection, const nds_device_transfer *transfer,
    nds_device_operation_result *result);

#ifdef __cplusplus
}
#endif

#endif
