#ifndef NDS_AICPU_ROCE_ABI_H
#define NDS_AICPU_ROCE_ABI_H

#include <stdint.h>

#define NDS_AICPU_ROCE_ABI_VERSION UINT32_C(6)
#define NDS_AICPU_ROCE_MAX_BYTES UINT64_C(65536)

enum nds_aicpu_rdma_opcode {
    /* Values deliberately match the CANN-9.0.0 HNS/verbs send-WR ABI. */
    NDS_AICPU_RDMA_WRITE = 0U,
    NDS_AICPU_SEND = 2U,
    NDS_AICPU_RDMA_READ = 4U,
};

/*
 * NDS-owned ABI for exactly one AICPU-side provider post. `ai_qp_address` is
 * returned by `RaAiQpCreate`; the local buffer must already be registered.
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
    uint32_t logical_device_id;
    uint64_t ai_qp_address;
    uint32_t local_lkey;
    uint32_t remote_rkey;
    uint64_t local_address;
    uint64_t remote_address;
    uint64_t length;
    uint64_t wr_id;
    uint64_t reserved_0;
    uint64_t reserved;
} nds_aicpu_rdma_post_request_v2;

#if defined(__cplusplus)
static_assert(sizeof(nds_aicpu_rdma_post_request_v2) == 80,
              "NDS AICPU RDMA-post ABI v6 must remain 80 bytes");
#else
_Static_assert(sizeof(nds_aicpu_rdma_post_request_v2) == 80,
               "NDS AICPU RDMA-post ABI v6 must remain 80 bytes");
#endif

#endif
