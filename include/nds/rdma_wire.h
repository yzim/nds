#ifndef NDS_RDMA_WIRE_H
#define NDS_RDMA_WIRE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NDS_RC_WIRE_MAGIC UINT32_C(0x4e445331) /* "NDS1" */
#define NDS_RC_WIRE_VERSION UINT16_C(2)
#define NDS_GID_BYTES 16U
#define NDS_MEMORY_WIRE_MAGIC UINT32_C(0x4e44534d) /* "NDSM" */
#define NDS_MEMORY_WIRE_VERSION UINT16_C(1)
#define NDS_MEMORY_ACCESS_REMOTE_WRITE UINT32_C(0x00000002)

/* Endpoint phase flags. Exactly one phase is required for wire v2. */
#define NDS_ENDPOINT_FLAG_QP_ONLY UINT16_C(0x0001)
#define NDS_ENDPOINT_FLAG_DATA_READY UINT16_C(0x0002)
#define NDS_ENDPOINT_FLAG_ALL (NDS_ENDPOINT_FLAG_QP_ONLY | NDS_ENDPOINT_FLAG_DATA_READY)

/*
 * Control-plane description of one RC RoCE endpoint. Integer fields are sent
 * in network byte order. `gid` is already a byte string and is copied intact.
 *
 * This protocol belongs to NDS, not HCCP or libibverbs. The eventual NPU
 * adapter translates between this representation and the selected HCCP ABI.
 */
typedef struct __attribute__((packed)) nds_rc_endpoint_wire_v1 {
    uint32_t magic;
    uint16_t version;
    uint16_t flags;
    uint32_t qp_num;
    uint32_t psn;
    uint32_t rkey;
    uint16_t port_num;
    uint16_t gid_index;
    uint32_t path_mtu;
    uint32_t access_flags;
    uint32_t traffic_class;
    uint32_t service_level;
    uint32_t retry_count;
    uint32_t retry_timeout;
    uint64_t address;
    uint8_t gid[NDS_GID_BYTES];
    uint8_t reserved[8];
} nds_rc_endpoint_wire_v1;


/*
 * Versioned memory descriptor exchanged only after QP establishment. It
 * contains project-owned public fields, never an RA or verbs private object.
 */
typedef struct __attribute__((packed)) nds_memory_descriptor_wire_v1 {
    uint32_t magic;
    uint16_t version;
    uint16_t flags;
    uint64_t transaction_id;
    uint64_t address;
    uint64_t length;
    uint32_t rkey;
    uint32_t access_flags;
    uint8_t reserved[8];
} nds_memory_descriptor_wire_v1;

#ifdef __cplusplus
static_assert(sizeof(nds_memory_descriptor_wire_v1) == 48,
              "NDS memory descriptor v1 must remain a fixed 48-byte message");
#else
_Static_assert(sizeof(nds_memory_descriptor_wire_v1) == 48,
               "NDS memory descriptor v1 must remain a fixed 48-byte message");
#endif

#ifdef __cplusplus
static_assert(sizeof(nds_rc_endpoint_wire_v1) == 80,
              "NDS RC endpoint v1 must remain a fixed 80-byte message");
#else
_Static_assert(sizeof(nds_rc_endpoint_wire_v1) == 80,
               "NDS RC endpoint v1 must remain a fixed 80-byte message");
#endif

#ifdef __cplusplus
}
#endif

#endif
