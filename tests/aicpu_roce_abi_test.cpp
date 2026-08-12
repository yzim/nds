#include "nds/aicpu_roce_abi.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>

int main()
{
    nds_aicpu_rdma_post_request_v2 request{};
    request.abi_version = NDS_AICPU_ROCE_ABI_VERSION;
    request.size = sizeof(request);
    request.opcode = NDS_AICPU_RDMA_WRITE;
    request.reserved_opcode = 0U;
    request.ai_qp_address = UINT64_C(0x12340000);
    request.local_lkey = 0x101U;
    request.remote_rkey = 0x202U;
    request.local_address = UINT64_C(0x123456780000);
    request.remote_address = UINT64_C(0x876543210000);
    request.length = 4096U;
    request.wr_id = UINT64_C(0xabc);
    request.reserved_0 = UINT64_C(0x123456789000);

    if (sizeof(request) != 80U || offsetof(nds_aicpu_rdma_post_request_v2, opcode) != 8U ||
        offsetof(nds_aicpu_rdma_post_request_v2, reserved_opcode) != 12U ||
        offsetof(nds_aicpu_rdma_post_request_v2, ai_qp_address) != 16U ||
        offsetof(nds_aicpu_rdma_post_request_v2, local_lkey) != 24U ||
        offsetof(nds_aicpu_rdma_post_request_v2, remote_rkey) != 28U ||
        offsetof(nds_aicpu_rdma_post_request_v2, local_address) != 32U ||
        offsetof(nds_aicpu_rdma_post_request_v2, remote_address) != 40U ||
        offsetof(nds_aicpu_rdma_post_request_v2, length) != 48U ||
        offsetof(nds_aicpu_rdma_post_request_v2, wr_id) != 56U ||
        offsetof(nds_aicpu_rdma_post_request_v2, reserved_0) != 64U ||
        request.abi_version != 5U || request.size != sizeof(request) ||
        request.opcode != NDS_AICPU_RDMA_WRITE || request.length > NDS_AICPU_ROCE_MAX_BYTES) {
        (void)std::fputs("NDS AICPU RDMA-post ABI v5 layout check failed\n", stderr);
        return 1;
    }
    return 0;
}
