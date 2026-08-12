#include "nds/aicpu_roce_abi.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>

int main()
{
    nds_aicpu_roce_write_request_v1 request{};
    request.abi_version = NDS_AICPU_ROCE_ABI_VERSION;
    request.size = sizeof(request);
    request.ai_qp_address = UINT64_C(0x12340000);
    request.local_lkey = 0x101U;
    request.remote_rkey = 0x202U;
    request.local_address = UINT64_C(0x123456780000);
    request.remote_address = UINT64_C(0x876543210000);
    request.length = 4096U;
    request.wr_id = UINT64_C(0xabc);

    if (sizeof(request) != 64U || offsetof(nds_aicpu_roce_write_request_v1, ai_qp_address) != 8U ||
        offsetof(nds_aicpu_roce_write_request_v1, local_lkey) != 16U ||
        offsetof(nds_aicpu_roce_write_request_v1, remote_rkey) != 20U ||
        offsetof(nds_aicpu_roce_write_request_v1, local_address) != 24U ||
        offsetof(nds_aicpu_roce_write_request_v1, remote_address) != 32U ||
        offsetof(nds_aicpu_roce_write_request_v1, length) != 40U ||
        offsetof(nds_aicpu_roce_write_request_v1, wr_id) != 48U ||
        request.abi_version != 1U || request.size != sizeof(request) || request.length > NDS_AICPU_ROCE_MAX_BYTES) {
        (void)std::fputs("NDS AICPU RoCE Write ABI v1 layout check failed\n", stderr);
        return 1;
    }
    return 0;
}
