#ifndef NDS_DEVICE_OPERATIONS_H
#define NDS_DEVICE_OPERATIONS_H

#include "nds/device_connection.h"

#include <stdint.h>

#define NDS_DEVICE_OPERATIONS_ABI_VERSION UINT32_C(3)

enum nds_device_operation {
    NDS_DEVICE_RDMA_SEND = 1U,
    NDS_DEVICE_RDMA_RECV = 2U,
    NDS_DEVICE_RDMA_READ = 3U,
    NDS_DEVICE_RDMA_WRITE = 4U,
    NDS_DEVICE_POLL_CQ = 5U,
};

typedef union nds_device_operation_parameters {
    nds_device_transfer transfer;
    nds_device_poll_cq_request poll_cq;
} nds_device_operation_parameters;

typedef struct nds_device_operation_request {
    uint32_t abi_version;
    uint32_t size;
    uint32_t operation;
    uint32_t reserved;
    nds_device_connection connection;
    nds_device_operation_parameters parameters;
    uint64_t operation_result_address;
} nds_device_operation_request;

#if defined(__cplusplus)
static_assert(sizeof(nds_device_operation_parameters) == 40, "device operation parameters ABI changed");
static_assert(sizeof(nds_device_operation_request) == 312, "device operation ABI changed");
#else
_Static_assert(sizeof(nds_device_operation_parameters) == 40, "device operation parameters ABI changed");
_Static_assert(sizeof(nds_device_operation_request) == 312, "device operation ABI changed");
#endif

#endif
