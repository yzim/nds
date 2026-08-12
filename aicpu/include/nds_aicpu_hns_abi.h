#ifndef NDS_AICPU_HNS_ABI_H
#define NDS_AICPU_HNS_ABI_H

/*
 * Minimal layouts used by CANN-9.0.0's device HNS provider. The AICPU sysroot
 * ships neither a public RNIC-post API nor libibverbs headers. This is a
 * version-pinned provider boundary, not a replacement libibverbs interface.
 */
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct nds_hns_sge {
    uint64_t addr;
    uint32_t length;
    uint32_t lkey;
};

struct nds_hns_send_wr {
    uint64_t wr_id;
    struct nds_hns_send_wr *next;
    struct nds_hns_sge *sg_list;
    int32_t num_sge;
    int32_t opcode;
    uint32_t send_flags;
    uint32_t imm_data;
    struct {
        uint64_t remote_addr;
        uint32_t rkey;
        uint32_t reserved;
    } rdma;
    uint8_t reserved[72];
};

struct nds_hns_post_send_response {
    unsigned int wqe_index;
    unsigned long db_info;
};

enum {
    NDS_HNS_WR_RDMA_WRITE = 0,
    NDS_HNS_WR_SEND = 2,
    NDS_HNS_WR_RDMA_READ = 4,
    NDS_HNS_SEND_SIGNALED = 1 << 1,
};

typedef int (*nds_hns_exp_post_send_fn)(
    void *qp, struct nds_hns_send_wr *wr, struct nds_hns_send_wr **bad_wr,
    struct nds_hns_post_send_response *response);


#if defined(__cplusplus)
static_assert(sizeof(nds_hns_sge) == 16, "HNS SGE ABI changed");
static_assert(sizeof(nds_hns_send_wr) == 128, "HNS send-WR ABI changed");
#else
_Static_assert(sizeof(struct nds_hns_sge) == 16, "HNS SGE ABI changed");
_Static_assert(sizeof(struct nds_hns_send_wr) == 128, "HNS send-WR ABI changed");
#endif

#ifdef __cplusplus
}
#endif

#endif
