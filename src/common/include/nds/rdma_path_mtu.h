#ifndef NDS_RDMA_PATH_MTU_H
#define NDS_RDMA_PATH_MTU_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Standard RC path-MTU byte values accepted by NDS's CPU verbs adapter. */
int nds_rdma_path_mtu_is_supported(uint32_t mtu_bytes);

/*
 * Select the CPU RC QP's RTR path MTU from its local active port value.
 *
 * HCOMM v9.0.0's RsDrvQpStateModifytoRtr() follows the same policy through
 * RsDrvSetMtu(): it obtains ibv_query_port(...).active_mtu locally and does
 * not derive the field from remote TypicalQp metadata. The HCCP TypicalQp ABI
 * contains no path-MTU member. Returns zero when local_active_mtu is not a
 * value NDS can map to libibverbs. `peer_reported_mtu` is deliberately
 * diagnostic-only and does not affect the result.
 */
uint32_t nds_cpu_qp_path_mtu_select(uint32_t local_active_mtu, uint32_t peer_reported_mtu);

#ifdef __cplusplus
}
#endif

#endif
