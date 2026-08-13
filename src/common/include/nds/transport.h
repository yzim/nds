#ifndef NDS_TRANSPORT_H
#define NDS_TRANSPORT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NDS_TRANSPORT_WIRE_MAGIC UINT32_C(0x4e445331) /* "NDS1" */
#define NDS_TRANSPORT_WIRE_VERSION UINT16_C(3)
#define NDS_GID_BYTES 16U

/*
 * Transport endpoint description of one RC RoCE endpoint. Integer fields are sent
 * in network byte order. `gid` is already a byte string and is copied intact.
 *
 * This transport record belongs to NDS, not HCCP or libibverbs. The eventual NPU
 * adapter translates between this representation and the selected HCCP ABI.
 */
typedef struct __attribute__((packed)) nds_transport_endpoint_wire {
    uint32_t magic;
    uint16_t version;
    uint16_t reserved0;
    uint32_t qp_num;
    uint32_t psn;
    uint16_t port_num;
    uint16_t gid_index;
    uint32_t path_mtu;
    uint32_t traffic_class;
    uint32_t service_level;
    uint32_t retry_count;
    uint32_t retry_timeout;
    uint8_t gid[NDS_GID_BYTES];
    uint8_t reserved[24];
} nds_transport_endpoint_wire;

/* Host-order endpoint metadata used by both the verbs endpoint and RA adapter. */
typedef struct nds_transport_endpoint {
    uint32_t qp_num;
    uint32_t psn;
    uint16_t port_num;
    uint16_t gid_index;
    /* Endpoint-reported MTU for diagnostics; not a cross-endpoint negotiation field. */
    uint32_t path_mtu;
    uint32_t traffic_class;
    uint32_t service_level;
    uint32_t retry_count;
    uint32_t retry_timeout;
    uint8_t gid[NDS_GID_BYTES];
} nds_transport_endpoint;

enum { NDS_TRANSPORT_ERROR_CAPACITY = 256 };

/* Encode/decode fixed-size transport endpoint metadata. All host fields remain host order. */
int nds_transport_endpoint_encode(const nds_transport_endpoint *endpoint, nds_transport_endpoint_wire *wire,
                                  char error[NDS_TRANSPORT_ERROR_CAPACITY]);
int nds_transport_endpoint_decode(const nds_transport_endpoint_wire *wire, nds_transport_endpoint *endpoint,
                                  char error[NDS_TRANSPORT_ERROR_CAPACITY]);

/* Standard RC path-MTU byte values accepted by NDS's CPU verbs adapter. */
int nds_transport_mtu_is_supported(uint32_t mtu_bytes);

/*
 * Select the CPU RC QP's RTR path MTU from its local active port value. The
 * peer-reported value is diagnostic-only and does not affect the result.
 */
uint32_t nds_transport_mtu_select(uint32_t local_active_mtu, uint32_t peer_reported_mtu);


#ifdef __cplusplus
static_assert(sizeof(nds_transport_endpoint_wire) == 80,
              "NDS RC endpoint must remain a fixed 80-byte message");
#else
_Static_assert(sizeof(nds_transport_endpoint_wire) == 80,
               "NDS RC endpoint must remain a fixed 80-byte message");
#endif

#ifdef __cplusplus
}
#endif

#endif
