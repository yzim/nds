#ifndef NDS_DEVICE_HNS_CODEC_H
#define NDS_DEVICE_HNS_CODEC_H

#include "nds/device_verbs.h"

#include <stdint.h>

typedef struct nds_hns_receive_segment {
    uint32_t length;
    uint32_t local_key;
    uint64_t address;
} nds_hns_receive_segment;

typedef struct nds_hns_cqe {
    uint32_t byte_4;
    uint32_t immediate_data;
    uint32_t byte_12;
    uint32_t byte_16;
    uint32_t byte_count;
    uint32_t reserved[3];
} nds_hns_cqe;

enum nds_hns_sq_opcode {
    NDS_HNS_SQ_SEND = 0x0U,
    NDS_HNS_SQ_RDMA_WRITE = 0x3U,
    NDS_HNS_SQ_RDMA_READ = 0x5U,
    NDS_HNS_SQ_INVALID = 0xffffffffU,
};

#define NDS_HNS_SQ_OPCODE_FROM_DEVICE(opcode)                                      \
    ((opcode) == NDS_DEVICE_WR_SEND                                                \
         ? NDS_HNS_SQ_SEND                                                        \
         : ((opcode) == NDS_DEVICE_WR_RDMA_WRITE                                  \
                ? NDS_HNS_SQ_RDMA_WRITE                                           \
                : ((opcode) == NDS_DEVICE_WR_RDMA_READ ? NDS_HNS_SQ_RDMA_READ     \
                                                       : NDS_HNS_SQ_INVALID)))

static inline int nds_hns_queue_has_space(uint32_t head, uint32_t tail,
                                          uint32_t depth, uint32_t reserved_entries) {
    return depth > reserved_entries && head - tail < depth - reserved_entries;
}

static inline void nds_hns_encode_receive_segment(nds_hns_receive_segment *segment,
                                                  uint64_t address, uint32_t length,
                                                  uint32_t local_key) {
    segment->length = length;
    segment->local_key = local_key;
    segment->address = address;
}

static inline int nds_hns_cqe_is_ready(const nds_hns_cqe *cqe, uint32_t consumer,
                                       uint32_t depth) {
    const uint32_t owner = (cqe->byte_4 >> 7U) & 1U;
    return depth != 0U && (owner ^ !!(consumer & depth)) != 0U;
}

static inline uint32_t nds_hns_send_tail_for_cqe(uint32_t tail, uint32_t depth,
                                                 const nds_hns_cqe *cqe) {
    const uint32_t wqe_index = cqe->byte_4 >> 16U;
    return tail + ((wqe_index - tail) & (depth - 1U));
}

static inline void nds_hns_decode_cqe(const nds_hns_cqe *cqe, uint64_t wr_id,
                                      nds_device_completion *completion) {
    completion->wr_id = wr_id;
    completion->status = (int32_t)((cqe->byte_4 >> 8U) & 0xffU);
    completion->opcode = (int32_t)(cqe->byte_4 & 0x1fU);
    completion->vendor_error = (cqe->byte_16 >> 24U) & 0xffU;
    completion->byte_length = cqe->byte_count;
    completion->qp_number = cqe->byte_12 & 0x00ffffffU;
    completion->flags = 0U;
    completion->immediate_data_or_invalidated_rkey = cqe->immediate_data;
    completion->reserved = 0U;
}

#if defined(__cplusplus)
static_assert(sizeof(nds_hns_receive_segment) == 16, "HNS receive segment ABI changed");
static_assert(sizeof(nds_hns_cqe) == 32, "HNS CQE ABI changed");
#else
_Static_assert(sizeof(nds_hns_receive_segment) == 16, "HNS receive segment ABI changed");
_Static_assert(sizeof(nds_hns_cqe) == 32, "HNS CQE ABI changed");
#endif

#endif
