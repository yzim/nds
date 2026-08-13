#include "nds/aiv_roce_abi.h"

#include <cstddef>
#include <cstdio>

int main() {
    nds_aiv_rdma_post_request request{};
    request.abi_version = NDS_AIV_ROCE_ABI_VERSION;
    request.size = sizeof(request);
    request.opcode = NDS_AIV_SEND;
    request.local_lkey = 0x101U;
    request.local_address = 0x123456780000U;
    request.length = 64U;
    request.post_count = 1U;

    if (sizeof(request) != 104U || offsetof(nds_aiv_rdma_post_request, opcode) != 64U ||
        offsetof(nds_aiv_rdma_post_request, local_lkey) != 68U ||
        offsetof(nds_aiv_rdma_post_request, remote_rkey) != 72U ||
        offsetof(nds_aiv_rdma_post_request, local_address) != 80U ||
        offsetof(nds_aiv_rdma_post_request, remote_address) != 88U ||
        offsetof(nds_aiv_rdma_post_request, length) != 96U || request.abi_version != 2U ||
        request.size != sizeof(request) || request.opcode != NDS_AIV_SEND || NDS_AIV_RDMA_WRITE != 3U) {
        (void)std::fputs("NDS AIV post ABI layout check failed\n", stderr);
        return 1;
    }
    return 0;
}
