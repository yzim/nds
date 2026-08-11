#ifndef NDS_RDMA_WIRE_CODEC_H
#define NDS_RDMA_WIRE_CODEC_H

#include "nds/rdma_wire.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Host-order representation used by both the verbs endpoint and RA adapter. */
typedef struct nds_rc_endpoint {
    uint16_t flags;
    uint32_t qp_num;
    uint32_t psn;
    uint32_t rkey;
    uint16_t port_num;
    uint16_t gid_index;
    /* Endpoint-reported MTU for diagnostics; not a cross-endpoint negotiation field. */
    uint32_t path_mtu;
    uint32_t access_flags;
    uint32_t traffic_class;
    uint32_t service_level;
    uint32_t retry_count;
    uint32_t retry_timeout;
    uint64_t address;
    uint8_t gid[NDS_GID_BYTES];
} nds_rc_endpoint;

typedef struct nds_memory_descriptor {
    uint16_t flags;
    uint64_t transaction_id;
    uint64_t address;
    uint64_t length;
    uint32_t rkey;
    uint32_t access_flags;
} nds_memory_descriptor;

enum { NDS_WIRE_ERROR_CAPACITY = 256 };

/* Encode/decode the fixed-size NDS wire record. All host fields remain host order. */
int nds_rc_endpoint_encode(const nds_rc_endpoint *endpoint, nds_rc_endpoint_wire_v1 *wire,
                           char error[NDS_WIRE_ERROR_CAPACITY]);
int nds_rc_endpoint_decode(const nds_rc_endpoint_wire_v1 *wire, nds_rc_endpoint *endpoint,
                           char error[NDS_WIRE_ERROR_CAPACITY]);
int nds_memory_descriptor_encode(const nds_memory_descriptor *descriptor,
                                 nds_memory_descriptor_wire_v1 *wire,
                                 char error[NDS_WIRE_ERROR_CAPACITY]);
int nds_memory_descriptor_decode(const nds_memory_descriptor_wire_v1 *wire,
                                 nds_memory_descriptor *descriptor,
                                 char error[NDS_WIRE_ERROR_CAPACITY]);

#ifdef __cplusplus
}
#endif

#endif
