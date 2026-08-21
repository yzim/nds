#ifndef NDS_HNS_HW_H
#define NDS_HNS_HW_H

/*
 * Hardware (NIC-side) HNS dataplane layout: the SQ WQE and CQE byte format
 * read/written by the HNS RNIC. Layouts are NDS-owned re-declarations learned
 * from HCOMM's vendored rdma-core fork
 * (third_party/rdma-core-42.7/providers/hns/hns_roce_u_hw_v2.h, RCWQE_*
 * fields) and the installed libhns on the target. AIV writes these bytes
 * directly; AICPU uses them only when the provider symbols are unavailable.
 */

#include "nds/device_verbs.h"

#include <stddef.h>
#include <stdint.h>

typedef struct NdsHnsHwWqeDataSeg {
    uint32_t length;
    uint32_t local_key;
    uint64_t address;
} NdsHnsHwWqeDataSeg;

typedef struct NdsHnsHwCqe {
    uint32_t byte_4;
    uint32_t immediate_data;
    uint32_t byte_12;
    uint32_t byte_16;
    uint32_t byte_count;
    uint32_t reserved[3];
} NdsHnsHwCqe;

enum NdsHnsHwSqOpcode {
    NDS_HNSHW_SQ_SEND = 0x0U,
    NDS_HNSHW_SQ_RDMA_WRITE = 0x3U,
    NDS_HNSHW_SQ_RDMA_READ = 0x5U,
    NDS_HNSHW_SQ_INVALID = 0xffffffffU,
};

/* HNS SQ WQE word0 bit 8: request a completion (SF). The AIV writer drives it
 * from NDS_DEVICE_SEND_SIGNALED so unsignaled WRs do not produce a CQE. */
enum NdsHnsHwSqFlag {
    NDS_HNSHW_SQ_SIGNALED = 1U << 8U,
};

#define NDS_HNSHW_SQ_OPCODE_FROM_DEVICE(opcode)    \
    ((opcode) == NDS_DEVICE_WR_SEND              \
         ? NDS_HNSHW_SQ_SEND                       \
         : ((opcode) == NDS_DEVICE_WR_RDMA_WRITE \
                ? NDS_HNSHW_SQ_RDMA_WRITE          \
                : ((opcode) == NDS_DEVICE_WR_RDMA_READ ? NDS_HNSHW_SQ_RDMA_READ : NDS_HNSHW_SQ_INVALID)))

static inline int nds_hns_hw_queue_has_space(uint32_t head, uint32_t tail, uint32_t depth, uint32_t reserved_entries) {
    return depth > reserved_entries && head - tail < depth - reserved_entries;
}

static inline void nds_hns_hw_encode_wqe_data_seg(NdsHnsHwWqeDataSeg *segment, uint64_t address, uint32_t length,
                                                  uint32_t local_key) {
    segment->length = length;
    segment->local_key = local_key;
    segment->address = address;
}

static inline int nds_hns_hw_cqe_is_ready(const NdsHnsHwCqe *cqe, uint32_t consumer, uint32_t depth) {
    const uint32_t owner = (cqe->byte_4 >> 7U) & 1U;
    return depth != 0U && (owner ^ !!(consumer & depth)) != 0U;
}

static inline uint32_t nds_hns_hw_send_tail_for_cqe(uint32_t tail, uint32_t depth, const NdsHnsHwCqe *cqe) {
    const uint32_t wqe_index = cqe->byte_4 >> 16U;
    return tail + ((wqe_index - tail) & (depth - 1U));
}

static inline void nds_hns_hw_decode_cqe(const NdsHnsHwCqe *cqe, uint64_t wr_id, NdsDeviceCompletion *completion) {
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
static_assert(sizeof(NdsHnsHwWqeDataSeg) == 16, "HNS receive segment ABI changed");
static_assert(offsetof(NdsHnsHwWqeDataSeg, length) == 0, "HNS receive length offset changed");
static_assert(offsetof(NdsHnsHwWqeDataSeg, local_key) == 4, "HNS receive local-key offset changed");
static_assert(offsetof(NdsHnsHwWqeDataSeg, address) == 8, "HNS receive address offset changed");
static_assert(sizeof(NdsHnsHwCqe) == 32, "HNS CQE ABI changed");
#else
_Static_assert(sizeof(NdsHnsHwWqeDataSeg) == 16, "HNS receive segment ABI changed");
_Static_assert(offsetof(NdsHnsHwWqeDataSeg, length) == 0, "HNS receive length offset changed");
_Static_assert(offsetof(NdsHnsHwWqeDataSeg, local_key) == 4, "HNS receive local-key offset changed");
_Static_assert(offsetof(NdsHnsHwWqeDataSeg, address) == 8, "HNS receive address offset changed");
_Static_assert(sizeof(NdsHnsHwCqe) == 32, "HNS CQE ABI changed");
#endif

#endif
