#ifndef NDS_DEVICE_VERBS_H
#define NDS_DEVICE_VERBS_H

#include "nds/device_qp.h"

#include <stdint.h>

#define NDS_DEVICE_VERBS_ABI_VERSION UINT32_C(1)
#define NDS_DEVICE_MAX_COMPLETIONS UINT32_C(16)

enum nds_device_queue_kind {
    NDS_DEVICE_SEND_QUEUE = 0U,
    NDS_DEVICE_RECEIVE_QUEUE = 1U,
};

enum nds_device_wr_opcode {
    NDS_DEVICE_WR_SEND = 0U,
    NDS_DEVICE_WR_RDMA_READ = 1U,
    NDS_DEVICE_WR_RDMA_WRITE = 2U,
};

enum nds_device_operation_status {
    NDS_DEVICE_OPERATION_SUCCESS = 0U,
    NDS_DEVICE_OPERATION_INVALID_ARGUMENT = 1U,
    NDS_DEVICE_OPERATION_SYMBOL_UNAVAILABLE = 2U,
    NDS_DEVICE_OPERATION_PROVIDER_FAILED = 3U,
    NDS_DEVICE_OPERATION_QUEUE_FULL = 4U,
    NDS_DEVICE_OPERATION_UNSUPPORTED = 5U,
};

enum nds_device_operation_path {
    NDS_DEVICE_OPERATION_PATH_NONE = 0U,
    NDS_DEVICE_OPERATION_PATH_DIRECT = 1U,
    NDS_DEVICE_OPERATION_PATH_PROVIDER = 2U,
};

enum nds_device_send_flags {
    NDS_DEVICE_SEND_SIGNALED = 1U << 0,
};

typedef struct nds_device_sge {
    uint64_t address;
    uint32_t length;
    uint32_t local_key;
} nds_device_sge;

typedef struct nds_device_send_wr {
    uint64_t wr_id;
    uint32_t opcode;
    uint32_t flags;
    nds_device_sge local;
    uint64_t remote_address;
    uint32_t remote_key;
    uint32_t reserved;
} nds_device_send_wr;

typedef struct nds_device_recv_wr {
    uint64_t wr_id;
    nds_device_sge local;
} nds_device_recv_wr;

typedef struct nds_device_poll_cq_request {
    uint32_t queue_kind;
    uint32_t max_completions;
    uint64_t completion_output_address;
} nds_device_poll_cq_request;

typedef struct nds_device_operation_result {
    uint32_t status;
    uint32_t path;
    int32_t provider_result;
    uint32_t reserved;
} nds_device_operation_result;

typedef struct nds_device_completion {
    uint64_t wr_id;
    int32_t status;
    int32_t opcode;
    uint32_t vendor_error;
    uint32_t byte_length;
    uint32_t qp_number;
    uint32_t flags;
    uint32_t immediate_data_or_invalidated_rkey;
    uint32_t reserved;
} nds_device_completion;

typedef struct nds_device_completion_output {
    uint32_t count;
    uint32_t reserved;
    nds_device_completion entries[NDS_DEVICE_MAX_COMPLETIONS];
} nds_device_completion_output;

#if defined(__cplusplus)
static_assert(sizeof(nds_device_sge) == 16, "device SGE ABI changed");
static_assert(sizeof(nds_device_send_wr) == 48, "device send WR ABI changed");
static_assert(sizeof(nds_device_recv_wr) == 24, "device receive WR ABI changed");
static_assert(sizeof(nds_device_poll_cq_request) == 16, "device CQ request ABI changed");
static_assert(sizeof(nds_device_completion) == 40, "device completion ABI changed");
static_assert(sizeof(nds_device_completion_output) == 648, "device completion output ABI changed");
static_assert(sizeof(nds_device_operation_result) == 16, "device operation result ABI changed");
#else
_Static_assert(sizeof(nds_device_sge) == 16, "device SGE ABI changed");
_Static_assert(sizeof(nds_device_send_wr) == 48, "device send WR ABI changed");
_Static_assert(sizeof(nds_device_recv_wr) == 24, "device receive WR ABI changed");
_Static_assert(sizeof(nds_device_poll_cq_request) == 16, "device CQ request ABI changed");
_Static_assert(sizeof(nds_device_completion) == 40, "device completion ABI changed");
_Static_assert(sizeof(nds_device_completion_output) == 648, "device completion output ABI changed");
_Static_assert(sizeof(nds_device_operation_result) == 16, "device operation result ABI changed");
#endif

#endif
