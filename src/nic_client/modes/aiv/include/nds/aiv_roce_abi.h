#ifndef NDS_AIV_ROCE_ABI_H
#define NDS_AIV_ROCE_ABI_H

#include <stdint.h>

#define NDS_AIV_ROCE_ABI_VERSION UINT32_C(1)

/*
 * NDS-owned, compact AIV request. The descriptor is copied from the HCCP
 * AI-QP result into ordinary NPU memory before the AIV launch. The AIV
 * kernel must never receive a host pointer to HCCP-owned metadata.
 */
typedef struct nds_aiv_sq_descriptor_v1 {
    uint32_t wqn;
    uint32_t reserved_0;
    uint64_t buffer_address;
    uint32_t wqebb_size;
    uint32_t depth;
    uint64_t head_address;
    uint64_t tail_address;
    uint64_t doorbell_address;
    uint32_t service_level;
    uint32_t reserved_1;
} nds_aiv_sq_descriptor_v1;

typedef struct nds_aiv_rdma_write_request_v1 {
    uint32_t abi_version;
    uint32_t size;
    nds_aiv_sq_descriptor_v1 send_queue;
    uint32_t local_lkey;
    uint32_t remote_rkey;
    uint64_t local_address;
    uint64_t remote_address;
    uint32_t length;
    uint32_t write_count;
} nds_aiv_rdma_write_request_v1;

#if defined(__cplusplus)
static_assert(sizeof(nds_aiv_sq_descriptor_v1) == 56, "NDS AIV SQ descriptor ABI must remain 56 bytes");
static_assert(sizeof(nds_aiv_rdma_write_request_v1) == 96, "NDS AIV Write request ABI must remain 96 bytes");
#else
_Static_assert(sizeof(nds_aiv_sq_descriptor_v1) == 56, "NDS AIV SQ descriptor ABI must remain 56 bytes");
_Static_assert(sizeof(nds_aiv_rdma_write_request_v1) == 96, "NDS AIV Write request ABI must remain 96 bytes");
#endif

#endif
