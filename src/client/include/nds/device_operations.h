#ifndef NDS_DEVICE_OPERATIONS_H
#define NDS_DEVICE_OPERATIONS_H

#include "nds/device_transport.h"

#include <stddef.h>
#include <stdint.h>

#define NDS_DEVICE_OPERATIONS_ABI_VERSION UINT32_C(4)

enum NdsDeviceOperation {
    NDS_DEVICE_RDMA_SEND = 1U,
    NDS_DEVICE_RDMA_RECV = 2U,
    NDS_DEVICE_RDMA_READ = 3U,
    NDS_DEVICE_RDMA_WRITE = 4U,
    NDS_DEVICE_POLL_CQ = 5U,
};

typedef union NdsDeviceOperationParameters {
    NdsDeviceSendWr send_wr;
    NdsDevicePollCqRequest poll_cq;
} NdsDeviceOperationParameters;

typedef struct NdsDeviceOperationRequest {
    uint32_t abi_version;
    uint32_t size;
    uint32_t operation;
    uint32_t reserved;
    NdsDeviceTransport transport;
    NdsDeviceOperationParameters parameters;
    uint64_t operation_result_address;
} NdsDeviceOperationRequest;

#if defined(__cplusplus)
static_assert(sizeof(NdsDeviceOperationParameters) == 40, "device operation parameters ABI changed");
static_assert(sizeof(NdsDeviceOperationRequest) == 312, "device operation ABI changed");
static_assert(offsetof(NdsDeviceOperationRequest, transport) == 16, "device operation transport offset changed");
#else
_Static_assert(sizeof(NdsDeviceOperationParameters) == 40, "device operation parameters ABI changed");
_Static_assert(sizeof(NdsDeviceOperationRequest) == 312, "device operation ABI changed");
_Static_assert(offsetof(NdsDeviceOperationRequest, transport) == 16, "device operation transport offset changed");
#endif

#endif
