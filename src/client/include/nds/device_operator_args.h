#ifndef NDS_DEVICE_OPERATOR_ARGS_H
#define NDS_DEVICE_OPERATOR_ARGS_H

#include "nds/device_transport.h"
#include "nds/device_storage.h"

#include <stdint.h>

#define NDS_DEVICE_OPERATOR_ARGS_ABI_VERSION UINT32_C(1)

typedef struct NdsDevicePostSendRequest {
    uint32_t abi_version;
    uint32_t size;
    NdsDeviceQp qp;
    NdsDeviceSendWr wr;
    uint64_t operation_result_address;
} NdsDevicePostSendRequest;

typedef struct NdsDevicePostRecvRequest {
    uint32_t abi_version;
    uint32_t size;
    NdsDeviceQp qp;
    NdsDeviceRecvWr wr;
    uint64_t operation_result_address;
} NdsDevicePostRecvRequest;

typedef struct NdsDevicePollCqOperatorRequest {
    uint32_t abi_version;
    uint32_t size;
    NdsDeviceQp qp;
    NdsDevicePollCqRequest poll_cq;
    uint64_t operation_result_address;
} NdsDevicePollCqOperatorRequest;

#if defined(__cplusplus)
static_assert(sizeof(NdsDevicePostSendRequest) == 304, "post-send operator ABI changed");
static_assert(sizeof(NdsDevicePostRecvRequest) == 280, "post-recv operator ABI changed");
static_assert(sizeof(NdsDevicePollCqOperatorRequest) == 272, "poll-CQ operator ABI changed");
#else
_Static_assert(sizeof(NdsDevicePostSendRequest) == 304, "post-send operator ABI changed");
_Static_assert(sizeof(NdsDevicePostRecvRequest) == 280, "post-recv operator ABI changed");
_Static_assert(sizeof(NdsDevicePollCqOperatorRequest) == 272, "poll-CQ operator ABI changed");
#endif

#endif
