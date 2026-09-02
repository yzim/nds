#ifndef NDS_BACKEND_VERBS_H
#define NDS_BACKEND_VERBS_H

#include <stddef.h>
#include <stdint.h>

#define NDS_MAX_COMPLETIONS UINT32_C(16)

enum NdsQpMode {
    NDS_QP_MODE_NORMAL = 0,
    NDS_QP_MODE_OPBASE = 2,
    NDS_QP_MODE_OPBASE_EXT = 4,
};

enum NdsDoorbellMode {
    NDS_DOORBELL_NONE = 0U,
    NDS_DOORBELL_RECORD = 1U,
    NDS_DOORBELL_MMIO = 2U,
};

enum NdsQpFlags {
    NDS_QP_CALLER_POLLS_CQ = 1U << 0,
};

typedef struct NdsWorkQueueDescriptor {
    uint32_t number;
    uint32_t depth;
    uint32_t entry_size;
    uint32_t doorbell_mode;
    uint64_t buffer_address;
    uint64_t head_address;
    uint64_t tail_address;
    uint64_t doorbell_address;
    uint64_t wr_id_address;
} NdsWorkQueueDescriptor;

typedef struct NdsCqDescriptor {
    uint32_t number;
    uint32_t depth;
    uint32_t entry_size;
    uint32_t doorbell_mode;
    uint64_t buffer_address;
    uint64_t consumer_address;
    uint64_t doorbell_address;
} NdsCqDescriptor;

typedef struct NdsQpDescriptor {
    uint32_t flags;
    int32_t qp_mode;
    uint32_t service_level;
    uint32_t reserved;
    uint64_t provider_qp_address;
    uint64_t provider_send_cq_address;
    uint64_t provider_receive_cq_address;
    NdsWorkQueueDescriptor send_queue;
    NdsWorkQueueDescriptor receive_queue;
    NdsCqDescriptor send_cq;
    NdsCqDescriptor receive_cq;
    /* Optional host-side handles used only by the RA host launcher. */
    uint64_t host_runtime_address;
    uint64_t host_qp_address;
} NdsQpDescriptor;

enum NdsWrOpcode {
    NDS_WR_SEND = 0U,
    NDS_WR_RDMA_READ = 1U,
    NDS_WR_RDMA_WRITE = 2U,
};

/* Negative values of these normalized codes are reported through an Args
 * envelope's return_value. They intentionally carry no provider diagnostics. */
enum NdsOperationStatus {
    NDS_OPERATION_SUCCESS = 0U,
    NDS_OPERATION_INVALID_ARGUMENT = 1U,
    NDS_OPERATION_SYMBOL_UNAVAILABLE = 2U,
    NDS_OPERATION_PROVIDER_FAILED = 3U,
    NDS_OPERATION_QUEUE_FULL = 4U,
    NDS_OPERATION_UNSUPPORTED = 5U,
};

enum NdsSendFlags {
    NDS_SEND_SIGNALED = 1U << 0,
};

/* Completion statuses exposed by the backend-neutral device WC record.
 * Backend adapters translate their provider status into this representation. */
enum NdsWcStatus {
    NDS_WC_SUCCESS = 0,
};

typedef struct NdsSge {
    uint64_t address;
    uint32_t length;
    uint32_t local_key;
} NdsSge;

/* One RDMA data-movement work request, shared across RA, AIV, and AICPU. It is
 * the caller-facing WR unit passed to the RA connection layer and carried by
 * the AIV/AICPU launch envelopes. Provider structs (NdsRaSendWr,
 * NdsHnsSendWr, the raw HNS WQE) are internal translations and are not
 * caller-visible. Callers should omit the ABI padding member `_reserved`. */
typedef struct NdsSendWr {
    uint64_t wr_id;
    uint32_t opcode;
    uint32_t flags;
    NdsSge local;
    uint64_t remote_address;
    uint32_t remote_key;
#ifdef __cplusplus
    uint32_t _reserved{};
#else
    uint32_t _reserved;
#endif
} NdsSendWr;

/* Receive WRs are always completion-producing when consumed; they do not have
 * a signal flag or a batch signal schedule. Transport code only tracks receive
 * credits and reclaims the resulting receive CQEs. */
typedef struct NdsRecvWr {
    uint64_t wr_id;
    NdsSge local;
} NdsRecvWr;

typedef struct NdsWc {
    uint64_t wr_id;
    int32_t status;
    int32_t opcode;
    uint32_t vendor_error;
    uint32_t byte_length;
    uint32_t qp_number;
    uint32_t flags;
    uint32_t immediate_data_or_invalidated_rkey;
    uint32_t reserved;
} NdsWc;

/* Operator-launch ABI envelopes. The payload fields mirror the verbs APIs;
 * they are not a second device-side verbs interface. Each envelope resides in
 * device-visible memory and return_value is its final named output member:
 * zero on success, a negative NDS operation error for post operations, or the
 * count of WCs written by PollCq. */
typedef struct NdsPostSendArgs {
    NdsQpDescriptor qp;
    NdsSendWr wr;
    int32_t return_value;
} NdsPostSendArgs;

/* Batch post-send launch envelope. wrs_address identifies a contiguous
 * device-global NdsSendWr array that remains valid until the launch
 * completes. The AIV implementation posts and rings the valid prefix once,
 * then reports its first invalid or unpostable WR through return_value and
 * bad_wr_address. The latter is zero on success. */
typedef struct NdsPostSendBatchArgs {
    NdsQpDescriptor qp;
    uint64_t wrs_address;
    uint32_t wr_count;
    int32_t return_value;
    uint64_t bad_wr_address;
} NdsPostSendBatchArgs;

typedef struct NdsPostRecvArgs {
    NdsQpDescriptor qp;
    NdsRecvWr wr;
    int32_t return_value;
} NdsPostRecvArgs;

typedef struct NdsPollCqArgs {
    NdsQpDescriptor qp;
    uint32_t is_send_cq;
    uint32_t max_completions;
    uint64_t wc_address;
    int32_t return_value;
} NdsPollCqArgs;

#if defined(__cplusplus)
static_assert(sizeof(NdsWorkQueueDescriptor) == 56, "device WQ ABI changed");
static_assert(sizeof(NdsCqDescriptor) == 40, "device CQ ABI changed");
static_assert(sizeof(NdsQpDescriptor) == 248, "device QP ABI changed");
static_assert(sizeof(NdsSge) == 16, "device SGE ABI changed");
static_assert(sizeof(NdsSendWr) == 48, "device send WR ABI changed");
static_assert(sizeof(NdsRecvWr) == 24, "device receive WR ABI changed");
static_assert(sizeof(NdsWc) == 40, "device WC ABI changed");
static_assert(sizeof(NdsPostSendArgs) == 304, "device post-send operator ABI changed");
static_assert(sizeof(NdsPostSendBatchArgs) == 272, "device post-send batch operator ABI changed");
static_assert(sizeof(NdsPostRecvArgs) == 280, "device post-recv operator ABI changed");
static_assert(sizeof(NdsPollCqArgs) == 272, "device poll-CQ operator ABI changed");
static_assert(offsetof(NdsPostSendArgs, return_value) > offsetof(NdsPostSendArgs, wr),
              "device post-send result must follow the request");
static_assert(offsetof(NdsPostRecvArgs, return_value) > offsetof(NdsPostRecvArgs, wr),
              "device post-recv result must follow the request");
static_assert(offsetof(NdsPollCqArgs, return_value) > offsetof(NdsPollCqArgs, wc_address),
              "device poll-CQ result must follow the request");
#else
_Static_assert(sizeof(NdsWorkQueueDescriptor) == 56, "device WQ ABI changed");
_Static_assert(sizeof(NdsCqDescriptor) == 40, "device CQ ABI changed");
_Static_assert(sizeof(NdsQpDescriptor) == 248, "device QP ABI changed");
_Static_assert(sizeof(NdsSge) == 16, "device SGE ABI changed");
_Static_assert(sizeof(NdsSendWr) == 48, "device send WR ABI changed");
_Static_assert(sizeof(NdsRecvWr) == 24, "device receive WR ABI changed");
_Static_assert(sizeof(NdsWc) == 40, "device WC ABI changed");
_Static_assert(sizeof(NdsPostSendArgs) == 304, "device post-send operator ABI changed");
_Static_assert(sizeof(NdsPostSendBatchArgs) == 272, "device post-send batch operator ABI changed");
_Static_assert(sizeof(NdsPostRecvArgs) == 280, "device post-recv operator ABI changed");
_Static_assert(sizeof(NdsPollCqArgs) == 272, "device poll-CQ operator ABI changed");
_Static_assert(offsetof(NdsPostSendArgs, return_value) > offsetof(NdsPostSendArgs, wr),
               "device post-send result must follow the request");
_Static_assert(offsetof(NdsPostRecvArgs, return_value) > offsetof(NdsPostRecvArgs, wr),
               "device post-recv result must follow the request");
_Static_assert(offsetof(NdsPollCqArgs, return_value) > offsetof(NdsPollCqArgs, wc_address),
               "device poll-CQ result must follow the request");
#endif

#endif
