#ifndef NDS_CONNECTION_H
#define NDS_CONNECTION_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NDS_QP_INFO_WIRE_MAGIC UINT32_C(0x4e445331) /* "NDS1" */
#define NDS_QP_INFO_WIRE_VERSION UINT16_C(3)
#define NDS_GID_BYTES 16U

/*
 * Peer RC QP identity exchanged on TCP so each side can connect. Integer
 * fields are sent in network byte order. `gid` is a byte string.
 *
 * This is QP addressing, not a storage MR and not an HCCP/libibverbs object.
 */
typedef struct __attribute__((packed)) nds_qp_info_wire {
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
} nds_qp_info_wire;

/* Host-order peer QP identity used by the CPU verbs backend and the NPU RA adapter. */
typedef struct nds_qp_info {
    uint32_t qp_num;
    uint32_t psn;
    uint16_t port_num;
    uint16_t gid_index;
    /* Reported MTU for diagnostics; not a cross-peer negotiation field. */
    uint32_t path_mtu;
    uint32_t traffic_class;
    uint32_t service_level;
    uint32_t retry_count;
    uint32_t retry_timeout;
    uint8_t gid[NDS_GID_BYTES];
} nds_qp_info;

enum nds_qp_info_result {
    NDS_QP_INFO_RESULT_OK = 0,
    NDS_QP_INFO_RESULT_INVALID_ARGUMENT = 1,
    NDS_QP_INFO_RESULT_INVALID_RECORD = 2,
};

enum nds_qp_info_result nds_qp_info_encode(const nds_qp_info *info, nds_qp_info_wire *wire);
enum nds_qp_info_result nds_qp_info_decode(const nds_qp_info_wire *wire, nds_qp_info *info);

/* Standard RC path-MTU byte values accepted by NDS's CPU verbs adapter. */
int nds_qp_mtu_is_supported(uint32_t mtu_bytes);

/*
 * Select the CPU RC QP's RTR path MTU from its local active port value. The
 * peer-reported value is diagnostic-only and does not affect the result.
 */
uint32_t nds_qp_mtu_select(uint32_t local_active_mtu, uint32_t peer_reported_mtu);

#ifdef __cplusplus
static_assert(sizeof(nds_qp_info_wire) == 80, "NDS RC QP info must remain a fixed 80-byte message");
#else
_Static_assert(sizeof(nds_qp_info_wire) == 80, "NDS RC QP info must remain a fixed 80-byte message");
#endif

#ifdef __cplusplus
}
#endif

#endif
