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

struct nds_hns_recv_wr {
    uint64_t wr_id;
    struct nds_hns_recv_wr *next;
    struct nds_hns_sge *sg_list;
    int32_t num_sge;
    uint32_t reserved;
};

struct nds_hns_wc {
    uint64_t wr_id;
    int32_t status;
    int32_t opcode;
    uint32_t vendor_err;
    uint32_t byte_len;
    uint32_t qp_num;
    uint32_t src_qp;
    uint32_t wc_flags;
    uint16_t pkey_index;
    uint16_t slid;
    uint8_t sl;
    uint8_t dlid_path_bits;
    uint8_t port_num;
    uint8_t reserved;
    uint32_t imm_data;
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

typedef int (*nds_hns_exp_post_send_fn)(void *qp, struct nds_hns_send_wr *wr, struct nds_hns_send_wr **bad_wr,
                                        struct nds_hns_post_send_response *response);
typedef int (*nds_hns_post_recv_fn)(void *qp, struct nds_hns_recv_wr *wr, struct nds_hns_recv_wr **bad_wr);
typedef int (*nds_hns_poll_cq_fn)(void *cq, int num_entries, struct nds_hns_wc *wc);

#if defined(__cplusplus)
static_assert(sizeof(nds_hns_sge) == 16, "HNS SGE ABI changed");
static_assert(sizeof(nds_hns_send_wr) == 128, "HNS send-WR ABI changed");
static_assert(sizeof(nds_hns_recv_wr) == 32, "HNS receive-WR ABI changed");
static_assert(sizeof(nds_hns_wc) == 48, "HNS completion ABI changed");
#else
_Static_assert(sizeof(struct nds_hns_sge) == 16, "HNS SGE ABI changed");
_Static_assert(sizeof(struct nds_hns_send_wr) == 128, "HNS send-WR ABI changed");
#endif

#ifdef __cplusplus
}
#endif

#endif
