#ifndef NDS_HNS_API_H
#define NDS_HNS_API_H

/*
 * Software-side HNS provider ABI (the ibv_exp_post_send / ibv_poll_cq
 * contract). Layouts are NDS-owned re-declarations of the version-pinned
 * CANN 9.0.0 provider boundary, learned from HCOMM's vendored rdma-core fork
 * (third_party/rdma-core-42.7/providers/hns/) and the installed libhns on the
 * target. The AICPU sysroot ships neither a public RNIC-post API nor
 * libibverbs headers. This is not a replacement libibverbs interface.
 */
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct NdsHnsSge {
    uint64_t addr;
    uint32_t length;
    uint32_t lkey;
};

struct NdsHnsSendWr {
    uint64_t wr_id;
    struct NdsHnsSendWr *next;
    struct NdsHnsSge *sg_list;
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

struct NdsHnsRecvWr {
    uint64_t wr_id;
    struct NdsHnsRecvWr *next;
    struct NdsHnsSge *sg_list;
    int32_t num_sge;
    uint32_t reserved;
};

struct NdsHnsWc {
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

struct NdsHnsPostSendResponse {
    unsigned int wqe_index;
    unsigned long db_info;
};

enum {
    NDS_HNS_WR_RDMA_WRITE = 0,
    NDS_HNS_WR_SEND = 2,
    NDS_HNS_WR_RDMA_READ = 4,
    NDS_HNS_SEND_SIGNALED = 1 << 1,
};

typedef int (*NdsHnsExpPostSendFn)(void *qp, struct NdsHnsSendWr *wr, struct NdsHnsSendWr **bad_wr,
                                        struct NdsHnsPostSendResponse *response);
typedef int (*NdsHnsPostRecvFn)(void *qp, struct NdsHnsRecvWr *wr, struct NdsHnsRecvWr **bad_wr);
typedef int (*NdsHnsPollCqFn)(void *cq, int num_entries, struct NdsHnsWc *wc);

#if defined(__cplusplus)
static_assert(sizeof(NdsHnsSge) == 16, "HNS SGE ABI changed");
static_assert(sizeof(NdsHnsSendWr) == 128, "HNS send-WR ABI changed");
static_assert(sizeof(NdsHnsRecvWr) == 32, "HNS receive-WR ABI changed");
static_assert(sizeof(NdsHnsWc) == 48, "HNS completion ABI changed");
#else
_Static_assert(sizeof(struct NdsHnsSge) == 16, "HNS SGE ABI changed");
_Static_assert(sizeof(struct NdsHnsSendWr) == 128, "HNS send-WR ABI changed");
#endif

#ifdef __cplusplus
}
#endif

#endif
