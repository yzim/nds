#include "nds/rdma_path_mtu.h"

int nds_rdma_path_mtu_is_supported(uint32_t mtu_bytes) {
    switch (mtu_bytes) {
        case 256U:
        case 512U:
        case 1024U:
        case 2048U:
        case 4096U:
            return 1;
        default:
            return 0;
    }
}

uint32_t nds_cpu_qp_path_mtu_select(uint32_t local_active_mtu, uint32_t peer_reported_mtu) {
    (void)peer_reported_mtu;
    return nds_rdma_path_mtu_is_supported(local_active_mtu) ? local_active_mtu : 0U;
}
