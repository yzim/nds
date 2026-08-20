#ifndef NDS_DEVICE_QP_H
#define NDS_DEVICE_QP_H

#include <stdint.h>

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

#if defined(__cplusplus)
static_assert(sizeof(NdsDeviceWorkQueue) == 56, "device WQ ABI changed");
static_assert(sizeof(NdsDeviceCompletionQueue) == 40, "device CQ ABI changed");
static_assert(sizeof(NdsDeviceQp) == 240, "device QP ABI changed");
#else
_Static_assert(sizeof(NdsDeviceWorkQueue) == 56, "device WQ ABI changed");
_Static_assert(sizeof(NdsDeviceCompletionQueue) == 40, "device CQ ABI changed");
_Static_assert(sizeof(NdsDeviceQp) == 240, "device QP ABI changed");
#endif

#endif
