#ifndef NDS_AICPU_ROCE_ABI_H
#define NDS_AICPU_ROCE_ABI_H

#include <stdint.h>

#define NDS_AICPU_ROCE_ABI_VERSION UINT32_C(1)
#define NDS_AICPU_ROCE_MAX_BYTES UINT64_C(65536)

/*
 * NDS-owned AICPU request ABI for one-way NPU-to-CPU RDMA Write.
 * All addresses and keys name already registered memory. `ai_qp_address` is
 * the opaque provider QP pointer returned by RaAiQpCreate. The kernel posts
 * exactly one signaled RDMA Write; it never waits for a peer flag or invokes
 * HCOMM/HCCL synchronization.
 */
typedef struct nds_aicpu_roce_write_request_v1 {
    uint32_t abi_version;
    uint32_t size;
    uint64_t ai_qp_address;
    uint32_t local_lkey;
    uint32_t remote_rkey;
    uint64_t local_address;
    uint64_t remote_address;
    uint64_t length;
    uint64_t wr_id;
    uint64_t reserved[1];
} nds_aicpu_roce_write_request_v1;

#if defined(__cplusplus)
static_assert(sizeof(nds_aicpu_roce_write_request_v1) == 64,
              "NDS AICPU RoCE write ABI v1 must remain 64 bytes");
#else
_Static_assert(sizeof(nds_aicpu_roce_write_request_v1) == 64,
               "NDS AICPU RoCE write ABI v1 must remain 64 bytes");
#endif

#endif
