#ifndef NDS_DEVICE_VERBS_H
#define NDS_DEVICE_VERBS_H

#include <stdint.h>

#define NDS_DEVICE_VERBS_ABI_VERSION UINT32_C(2)
#define NDS_DEVICE_MAX_COMPLETIONS UINT32_C(16)
#define NDS_DEVICE_QP_ABI_VERSION UINT32_C(1)

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

typedef struct NdsDeviceCompletionQueue {
    uint32_t number;
    uint32_t depth;
    uint32_t entry_size;
    uint32_t doorbell_mode;
    uint64_t buffer_address;
    uint64_t consumer_address;
    uint64_t doorbell_address;
} NdsDeviceCompletionQueue;

typedef struct NdsDeviceQp {
    uint32_t abi_version;
    uint32_t size;
    uint32_t flags;
    int32_t qp_mode;
    uint32_t service_level;
    uint32_t reserved;
    uint64_t provider_qp_address;
    uint64_t provider_send_cq_address;
    uint64_t provider_receive_cq_address;
    NdsDeviceWorkQueue send_queue;
    NdsDeviceWorkQueue receive_queue;
    NdsDeviceCompletionQueue send_cq;
    NdsDeviceCompletionQueue receive_cq;
} NdsDeviceQp;

enum NdsDeviceWrOpcode {
    NDS_DEVICE_WR_SEND = 0U,
    NDS_DEVICE_WR_RDMA_READ = 1U,
    NDS_DEVICE_WR_RDMA_WRITE = 2U,
};

enum NdsDeviceOperationStatus {
    NDS_DEVICE_OPERATION_SUCCESS = 0U,
    NDS_DEVICE_OPERATION_INVALID_ARGUMENT = 1U,
    NDS_DEVICE_OPERATION_SYMBOL_UNAVAILABLE = 2U,
    NDS_DEVICE_OPERATION_PROVIDER_FAILED = 3U,
    NDS_DEVICE_OPERATION_QUEUE_FULL = 4U,
    NDS_DEVICE_OPERATION_UNSUPPORTED = 5U,
};

enum NdsDeviceOperationPath {
    NDS_DEVICE_OPERATION_PATH_NONE = 0U,
    NDS_DEVICE_OPERATION_PATH_DIRECT = 1U,
    NDS_DEVICE_OPERATION_PATH_PROVIDER = 2U,
};

enum NdsDeviceOperationFlags {
    NDS_DEVICE_OPERATION_TERMINAL = 1U << 0,
};

enum NdsDeviceSendFlags {
    NDS_DEVICE_SEND_SIGNALED = 1U << 0,
};

typedef struct NdsDeviceSge {
    uint64_t address;
    uint32_t length;
    uint32_t local_key;
} NdsDeviceSge;

/* One RDMA data-movement work request, shared across RA, AIV, and AICPU. It is
 * the caller-facing WR unit carried in NdsDeviceOperationRequest for AIV/AICPU
 * and passed to the RA connection layer. Provider structs (NdsRaSendWr,
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

typedef struct NdsDevicePollCqRequest {
    uint32_t is_send_cq;
    uint32_t max_completions;
    uint64_t completion_output_address;
} NdsDevicePollCqRequest;

typedef struct NdsDeviceOperationResult {
    uint32_t status;
    uint32_t path;
    int32_t provider_result;
    uint32_t reserved;
} NdsDeviceOperationResult;

typedef struct NdsDeviceCompletion {
    uint64_t wr_id;
    int32_t status;
    int32_t opcode;
    uint32_t vendor_error;
    uint32_t byte_length;
    uint32_t qp_number;
    uint32_t flags;
    uint32_t immediate_data_or_invalidated_rkey;
    uint32_t reserved;
} NdsDeviceCompletion;

typedef struct NdsDeviceCompletionOutput {
    uint32_t count;
    uint32_t reserved;
    NdsDeviceCompletion entries[NDS_DEVICE_MAX_COMPLETIONS];
} NdsDeviceCompletionOutput;

#if defined(__cplusplus)
static_assert(sizeof(NdsDeviceWorkQueue) == 56, "device WQ ABI changed");
static_assert(sizeof(NdsDeviceCompletionQueue) == 40, "device CQ ABI changed");
static_assert(sizeof(NdsDeviceQp) == 240, "device QP ABI changed");
static_assert(sizeof(NdsDeviceSge) == 16, "device SGE ABI changed");
static_assert(sizeof(NdsDeviceSendWr) == 48, "device send WR ABI changed");
static_assert(sizeof(NdsDeviceRecvWr) == 24, "device receive WR ABI changed");
static_assert(sizeof(NdsDevicePollCqRequest) == 16, "device CQ request ABI changed");
static_assert(sizeof(NdsDeviceCompletion) == 40, "device completion ABI changed");
static_assert(sizeof(NdsDeviceCompletionOutput) == 648, "device completion output ABI changed");
static_assert(sizeof(NdsDeviceOperationResult) == 16, "device operation result ABI changed");
#else
_Static_assert(sizeof(NdsDeviceWorkQueue) == 56, "device WQ ABI changed");
_Static_assert(sizeof(NdsDeviceCompletionQueue) == 40, "device CQ ABI changed");
_Static_assert(sizeof(NdsDeviceQp) == 240, "device QP ABI changed");
_Static_assert(sizeof(NdsDeviceSge) == 16, "device SGE ABI changed");
_Static_assert(sizeof(NdsDeviceSendWr) == 48, "device send WR ABI changed");
_Static_assert(sizeof(NdsDeviceRecvWr) == 24, "device receive WR ABI changed");
_Static_assert(sizeof(NdsDevicePollCqRequest) == 16, "device CQ request ABI changed");
_Static_assert(sizeof(NdsDeviceCompletion) == 40, "device completion ABI changed");
_Static_assert(sizeof(NdsDeviceCompletionOutput) == 648, "device completion output ABI changed");
_Static_assert(sizeof(NdsDeviceOperationResult) == 16, "device operation result ABI changed");
#endif

#endif
