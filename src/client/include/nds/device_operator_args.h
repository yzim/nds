#ifndef NDS_DEVICE_OPERATOR_ARGS_H
#define NDS_DEVICE_OPERATOR_ARGS_H

#include "nds/device_connection.h"
#include "nds/device_storage.h"

#include <stdint.h>

#define NDS_DEVICE_OPERATOR_ARGS_ABI_VERSION UINT32_C(1)

typedef struct nds_device_post_send_request {
    uint32_t abi_version;
    uint32_t size;
    nds_device_qp qp;
    nds_device_send_wr wr;
    uint64_t operation_result_address;
} nds_device_post_send_request;

typedef struct nds_device_post_recv_request {
    uint32_t abi_version;
    uint32_t size;
    nds_device_qp qp;
    nds_device_recv_wr wr;
    uint64_t operation_result_address;
} nds_device_post_recv_request;

typedef struct nds_device_poll_cq_operator_request {
    uint32_t abi_version;
    uint32_t size;
    nds_device_qp qp;
    nds_device_poll_cq_request poll_cq;
    uint64_t operation_result_address;
} nds_device_poll_cq_operator_request;

#if defined(__cplusplus)
static_assert(sizeof(nds_device_post_send_request) == 304, "post-send operator ABI changed");
static_assert(sizeof(nds_device_post_recv_request) == 280, "post-recv operator ABI changed");
static_assert(sizeof(nds_device_poll_cq_operator_request) == 272, "poll-CQ operator ABI changed");
#else
_Static_assert(sizeof(nds_device_post_send_request) == 304, "post-send operator ABI changed");
_Static_assert(sizeof(nds_device_post_recv_request) == 280, "post-recv operator ABI changed");
_Static_assert(sizeof(nds_device_poll_cq_operator_request) == 272, "poll-CQ operator ABI changed");
#endif

#endif
