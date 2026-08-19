#ifndef NDS_DEVICE_QP_H
#define NDS_DEVICE_QP_H

#include <stdint.h>

#define NDS_DEVICE_QP_ABI_VERSION UINT32_C(1)

enum nds_device_qp_mode {
    NDS_DEVICE_QP_MODE_NORMAL = 0,
    NDS_DEVICE_QP_MODE_OPBASE = 2,
    NDS_DEVICE_QP_MODE_OPBASE_EXT = 4,
};

enum nds_device_doorbell_mode {
    NDS_DEVICE_DOORBELL_NONE = 0U,
    NDS_DEVICE_DOORBELL_RECORD = 1U,
    NDS_DEVICE_DOORBELL_MMIO = 2U,
};

enum nds_device_qp_flags {
    NDS_DEVICE_QP_CALLER_POLLS_CQ = 1U << 0,
};

typedef struct nds_device_work_queue {
    uint32_t number;
    uint32_t depth;
    uint32_t entry_size;
    uint32_t doorbell_mode;
    uint64_t buffer_address;
    uint64_t head_address;
    uint64_t tail_address;
    uint64_t doorbell_address;
    uint64_t wr_id_address;
} nds_device_work_queue;

typedef struct nds_device_completion_queue {
    uint32_t number;
    uint32_t depth;
    uint32_t entry_size;
    uint32_t doorbell_mode;
    uint64_t buffer_address;
    uint64_t consumer_address;
    uint64_t doorbell_address;
} nds_device_completion_queue;

typedef struct nds_device_qp {
    uint32_t abi_version;
    uint32_t size;
    uint32_t flags;
    int32_t qp_mode;
    uint32_t service_level;
    uint32_t reserved;
    uint64_t provider_qp_address;
    uint64_t provider_send_cq_address;
    uint64_t provider_receive_cq_address;
    nds_device_work_queue send_queue;
    nds_device_work_queue receive_queue;
    nds_device_completion_queue send_cq;
    nds_device_completion_queue receive_cq;
} nds_device_qp;

#if defined(__cplusplus)
static_assert(sizeof(nds_device_work_queue) == 56, "device WQ ABI changed");
static_assert(sizeof(nds_device_completion_queue) == 40, "device CQ ABI changed");
static_assert(sizeof(nds_device_qp) == 240, "device QP ABI changed");
#else
_Static_assert(sizeof(nds_device_work_queue) == 56, "device WQ ABI changed");
_Static_assert(sizeof(nds_device_completion_queue) == 40, "device CQ ABI changed");
_Static_assert(sizeof(nds_device_qp) == 240, "device QP ABI changed");
#endif

#endif
