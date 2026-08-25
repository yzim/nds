#ifndef NDS_DEVICE_VERBS_H
#define NDS_DEVICE_VERBS_H

#include <stdint.h>

#define NDS_DEVICE_MAX_COMPLETIONS UINT32_C(16)

enum NdsDeviceQpMode {
    NDS_DEVICE_QP_MODE_NORMAL = 0,
    NDS_DEVICE_QP_MODE_OPBASE = 2,
    NDS_DEVICE_QP_MODE_OPBASE_EXT = 4,
};

enum NdsDeviceDoorbellMode {
    NDS_DEVICE_DOORBELL_NONE = 0U,
    NDS_DEVICE_DOORBELL_RECORD = 1U,
    NDS_DEVICE_DOORBELL_MMIO = 2U,
};

enum NdsDeviceQpFlags {
    NDS_DEVICE_QP_CALLER_POLLS_CQ = 1U << 0,
};

typedef struct NdsDeviceWorkQueue {
    uint32_t number;
    uint32_t depth;
    uint32_t entry_size;
    uint32_t doorbell_mode;
    uint64_t buffer_address;
    uint64_t head_address;
    uint64_t tail_address;
    uint64_t doorbell_address;
    uint64_t wr_id_address;
} NdsDeviceWorkQueue;

typedef struct NdsDeviceCq {
    uint32_t number;
    uint32_t depth;
    uint32_t entry_size;
    uint32_t doorbell_mode;
    uint64_t buffer_address;
    uint64_t consumer_address;
    uint64_t doorbell_address;
} NdsDeviceCq;

typedef struct NdsDeviceQp {
    uint32_t flags;
    int32_t qp_mode;
    uint32_t service_level;
    uint32_t doorbell_index;
    uint64_t provider_qp_address;
    uint64_t provider_send_cq_address;
    uint64_t provider_receive_cq_address;
    NdsDeviceWorkQueue send_queue;
    NdsDeviceWorkQueue receive_queue;
    NdsDeviceCq send_cq;
    NdsDeviceCq receive_cq;
} NdsDeviceQp;

enum NdsDeviceWrOpcode {
    NDS_DEVICE_WR_SEND = 0U,
    NDS_DEVICE_WR_RDMA_READ = 1U,
    NDS_DEVICE_WR_RDMA_WRITE = 2U,
};

/* Negative values of these normalized codes are reported through an Args
 * envelope's return_value. They intentionally carry no provider diagnostics. */
enum NdsDeviceOperationStatus {
    NDS_DEVICE_OPERATION_SUCCESS = 0U,
    NDS_DEVICE_OPERATION_INVALID_ARGUMENT = 1U,
    NDS_DEVICE_OPERATION_SYMBOL_UNAVAILABLE = 2U,
    NDS_DEVICE_OPERATION_PROVIDER_FAILED = 3U,
    NDS_DEVICE_OPERATION_QUEUE_FULL = 4U,
    NDS_DEVICE_OPERATION_UNSUPPORTED = 5U,
};

enum NdsDeviceSendFlags {
    NDS_DEVICE_SEND_SIGNALED = 1U << 0,
    NDS_DEVICE_SEND_DEFER_DOORBELL = 1U << 1,
};

typedef struct NdsDeviceSge {
    uint64_t address;
    uint32_t length;
    uint32_t local_key;
} NdsDeviceSge;

/* One RDMA data-movement work request, shared across RA, AIV, and AICPU. It is
 * the caller-facing WR unit passed to the RA connection layer and carried by
 * the AIV/AICPU launch envelopes. Provider structs (NdsRaSendWr,
 * NdsHnsSendWr, the raw HNS WQE) are internal translations and are not
 * caller-visible. */
typedef struct NdsDeviceSendWr {
    uint64_t wr_id;
    uint32_t opcode;
    uint32_t flags;
    NdsDeviceSge local;
    uint64_t remote_address;
    uint32_t remote_key;
    uint32_t reserved;
} NdsDeviceSendWr;

typedef struct NdsDeviceRecvWr {
    uint64_t wr_id;
    NdsDeviceSge local;
} NdsDeviceRecvWr;

typedef struct NdsDeviceWc {
    uint64_t wr_id;
    int32_t status;
    int32_t opcode;
    uint32_t vendor_error;
    uint32_t byte_length;
    uint32_t qp_number;
    uint32_t flags;
    uint32_t immediate_data_or_invalidated_rkey;
    uint32_t reserved;
} NdsDeviceWc;

/* The standard-CP1 provider returns this descriptor after it prepares an
 * OPBASE WQE.  The host may submit it through the public runtime doorbell ABI.
 */
typedef struct NdsDeviceDoorbell {
    uint32_t index;
    uint32_t reserved;
    uint64_t info;
} NdsDeviceDoorbell;

/* Operator-launch ABI envelopes. The payload fields mirror the verbs APIs;
 * they are not a second device-side verbs interface. return_value is zero on
 * success, a negative NDS operation error for post operations, or the count
 * of WCs written by PollCq. */
typedef struct NdsDevicePostSendArgs {
    NdsDeviceQp qp;
    NdsDeviceSendWr wr;
    int32_t return_value;
    uint32_t reserved;
    uint64_t doorbell_address;
    uint64_t reserved2;
} NdsDevicePostSendArgs;

/* Posts a contiguous sequence of ordinary provider WQEs from AICPU.  In
 * OPBASE_EXT mode the final prepared WQE descriptor is written to
 * doorbell_address for one host-side rtRDMADBSend call. */
typedef struct NdsDevicePostSendBatchArgs {
    NdsDeviceQp qp;
    uint64_t local_address;
    uint64_t remote_address;
    uint64_t wr_id_start;
    uint32_t local_key;
    uint32_t remote_key;
    uint32_t length;
    uint32_t count;
    uint32_t signal_every;
    int32_t return_value;
    uint32_t reserved;
    uint64_t doorbell_address;
    uint64_t reserved2;
} NdsDevicePostSendBatchArgs;

typedef struct NdsDevicePostRecvArgs {
    NdsDeviceQp qp;
    NdsDeviceRecvWr wr;
    int32_t return_value;
    uint32_t reserved;
} NdsDevicePostRecvArgs;

typedef struct NdsDevicePollCqArgs {
    NdsDeviceQp qp;
    uint32_t is_send_cq;
    uint32_t max_completions;
    uint64_t wc_address;
    int32_t return_value;
    uint32_t reserved;
} NdsDevicePollCqArgs;

#if defined(__cplusplus)
static_assert(sizeof(NdsDeviceWorkQueue) == 56, "device WQ ABI changed");
static_assert(sizeof(NdsDeviceCq) == 40, "device CQ ABI changed");
static_assert(sizeof(NdsDeviceQp) == 232, "device QP ABI changed");
static_assert(sizeof(NdsDeviceSge) == 16, "device SGE ABI changed");
static_assert(sizeof(NdsDeviceSendWr) == 48, "device send WR ABI changed");
static_assert(sizeof(NdsDeviceRecvWr) == 24, "device receive WR ABI changed");
static_assert(sizeof(NdsDeviceWc) == 40, "device WC ABI changed");
static_assert(sizeof(NdsDeviceDoorbell) == 16, "device doorbell ABI changed");
static_assert(sizeof(NdsDevicePostSendArgs) == 304, "device post-send operator ABI changed");
static_assert(sizeof(NdsDevicePostSendBatchArgs) == 304, "device batch-post operator ABI changed");
static_assert(sizeof(NdsDevicePostRecvArgs) == 264, "device post-recv operator ABI changed");
static_assert(sizeof(NdsDevicePollCqArgs) == 256, "device poll-CQ operator ABI changed");
#else
_Static_assert(sizeof(NdsDeviceWorkQueue) == 56, "device WQ ABI changed");
_Static_assert(sizeof(NdsDeviceCq) == 40, "device CQ ABI changed");
_Static_assert(sizeof(NdsDeviceQp) == 232, "device QP ABI changed");
_Static_assert(sizeof(NdsDeviceSge) == 16, "device SGE ABI changed");
_Static_assert(sizeof(NdsDeviceSendWr) == 48, "device send WR ABI changed");
_Static_assert(sizeof(NdsDeviceRecvWr) == 24, "device receive WR ABI changed");
_Static_assert(sizeof(NdsDeviceWc) == 40, "device WC ABI changed");
_Static_assert(sizeof(NdsDeviceDoorbell) == 16, "device doorbell ABI changed");
_Static_assert(sizeof(NdsDevicePostSendArgs) == 304, "device post-send operator ABI changed");
_Static_assert(sizeof(NdsDevicePostSendBatchArgs) == 304, "device batch-post operator ABI changed");
_Static_assert(sizeof(NdsDevicePostRecvArgs) == 264, "device post-recv operator ABI changed");
_Static_assert(sizeof(NdsDevicePollCqArgs) == 256, "device poll-CQ operator ABI changed");
#endif

#endif
