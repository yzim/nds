#ifndef NDS_AICPU_ROCE_ABI_H
#define NDS_AICPU_ROCE_ABI_H

#include <stdint.h>

#define NDS_AICPU_ROCE_ABI_VERSION UINT32_C(3)
#define NDS_AICPU_ROCE_MAX_BYTES UINT64_C(65536)

enum nds_aicpu_rdma_opcode {
    /* Values deliberately match the CANN-9.0.0 HNS/verbs send-WR ABI. */
    NDS_AICPU_RDMA_WRITE = 0U,
    NDS_AICPU_SEND = 2U,
    NDS_AICPU_RDMA_READ = 4U,
};

/* Device-visible progress/error values written through status_device_address. */
enum nds_aicpu_rdma_post_status {
    NDS_AICPU_RDMA_POST_STATUS_NONE = 0U,
    NDS_AICPU_RDMA_POST_STATUS_ENTERED = 0x4e445301U,
    NDS_AICPU_RDMA_POST_STATUS_INVALID_ARGUMENT = 0x4e445302U,
    NDS_AICPU_RDMA_POST_STATUS_PROVIDER_UNAVAILABLE = 0x4e445303U,
    NDS_AICPU_RDMA_POST_STATUS_DOORBELL_UNAVAILABLE = 0x4e445304U,
    NDS_AICPU_RDMA_POST_STATUS_POST_FAILED = 0x4e445305U,
    NDS_AICPU_RDMA_POST_STATUS_POSTED = 0x4e445306U,
    NDS_AICPU_RDMA_POST_STATUS_DOORBELL_FAILED = 0x4e445307U,
    NDS_AICPU_RDMA_POST_STATUS_SUCCESS = 0x4e445308U,
};

/*
 * NDS-owned ABI for exactly one AICPU-side provider post plus its RNIC
 * doorbell submission. `status_device_address`, when nonzero, names a NPU
 * uint32_t used only to report the kernel progress/error checkpoint.
 * `ai_qp_address` and `db_index` are returned by `RaAiQpCreate`. The local
 * buffer must already be registered.
 *
 * RDMA_WRITE and RDMA_READ require nonzero remote_address and remote_rkey.
 * SEND uses only the local SGE; remote_address and remote_rkey must be zero.
 * This is intentionally not an HCOMM protocol: it contains no flags, peer
 * acknowledgement, batching, rank state, or completion-poller state.
 */
typedef struct nds_aicpu_rdma_post_request_v2 {
    uint32_t abi_version;
    uint32_t size;
    uint32_t opcode;
    uint32_t db_index;
    uint64_t ai_qp_address;
    uint32_t local_lkey;
    uint32_t remote_rkey;
    uint64_t local_address;
    uint64_t remote_address;
    uint64_t length;
    uint64_t wr_id;
    uint64_t status_device_address;
    uint64_t reserved;
} nds_aicpu_rdma_post_request_v2;

#if defined(__cplusplus)
static_assert(sizeof(nds_aicpu_rdma_post_request_v2) == 80,
              "NDS AICPU RDMA-post ABI v3 must remain 80 bytes");
#else
_Static_assert(sizeof(nds_aicpu_rdma_post_request_v2) == 80,
               "NDS AICPU RDMA-post ABI v3 must remain 80 bytes");
#endif

#endif
