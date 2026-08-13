#ifndef NDS_RDMA_WIRE_H
#define NDS_RDMA_WIRE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NDS_RC_WIRE_MAGIC UINT32_C(0x4e445331) /* "NDS1" */
#define NDS_RC_WIRE_VERSION UINT16_C(3)
#define NDS_GID_BYTES 16U

/*
 * Peer-exchange description of one RC RoCE endpoint. Integer fields are sent
 * in network byte order. `gid` is already a byte string and is copied intact.
 *
 * This protocol belongs to NDS, not HCCP or libibverbs. The eventual NPU
 * adapter translates between this representation and the selected HCCP ABI.
 */
typedef struct __attribute__((packed)) nds_rc_endpoint_wire {
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
} nds_rc_endpoint_wire;


#ifdef __cplusplus
static_assert(sizeof(nds_rc_endpoint_wire) == 80,
              "NDS RC endpoint must remain a fixed 80-byte message");
#else
_Static_assert(sizeof(nds_rc_endpoint_wire) == 80,
               "NDS RC endpoint must remain a fixed 80-byte message");
#endif

#ifdef __cplusplus
}
#endif

#endif
