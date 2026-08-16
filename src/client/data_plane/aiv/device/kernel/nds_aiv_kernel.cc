#define NDS_AIV_DEVICE_API_LINKAGE static

#include "kernel_operator.h"
#include "nds/aiv_device_api.h"

#include "../qp.cc"
#include "../connection.cc"
#include "../storage.cc"

using namespace AscendC;

#define NDS_EXPORT_AIV_META_INFO(kernel_name)                                           \
    static const struct FunLevelKType kernel_name##_kernel_type_section __attribute__(( \
        used, section(".ascend.meta." #kernel_name))) = {{F_TYPE_KTYPE, sizeof(unsigned int), K_TYPE_AIV}}

namespace {
__aicore__ inline void SetInvalid(__gm__ nds_device_operation_result *result) {
    result->status = NDS_DEVICE_OPERATION_INVALID_ARGUMENT;
    result->path = NDS_DEVICE_OPERATION_PATH_NONE;
    result->provider_result = 0;
    result->reserved = 0U;
}
}  // namespace

extern "C" __global__ __aicore__ void NdsAivConnectionOp(GM_ADDR request_address) {
    __gm__ nds_device_operation_request *request =
        reinterpret_cast<__gm__ nds_device_operation_request *>(request_address);
    if (request == nullptr || request->operation_result_address == 0U) return;
    __gm__ nds_device_operation_result *result = reinterpret_cast<__gm__ nds_device_operation_result *>(
        request->operation_result_address);
    if (request->abi_version != NDS_DEVICE_OPERATIONS_ABI_VERSION || request->size != sizeof(*request) ||
        request->connection.abi_version != NDS_DEVICE_CONNECTION_ABI_VERSION) {
        SetInvalid(result);
        return;
    }
    TPipe pipe;
    TBuf<> scratch;
    pipe.InitBuffer(scratch, 64U);
    if (request->operation == NDS_DEVICE_RDMA_SEND)
        NdsAivRdmaSend(&request->connection, &request->parameters.transfer, &scratch, result);
    else if (request->operation == NDS_DEVICE_RDMA_RECV)
        NdsAivRdmaRecv(&request->connection, &request->parameters.transfer, &scratch, result);
    else if (request->operation == NDS_DEVICE_RDMA_READ)
        NdsAivRdmaRead(&request->connection, &request->parameters.transfer, &scratch, result);
    else if (request->operation == NDS_DEVICE_RDMA_WRITE)
        NdsAivRdmaWrite(&request->connection, &request->parameters.transfer, &scratch, result);
    else if (request->operation == NDS_DEVICE_POLL_CQ)
        NdsAivPollCq(&request->connection.qp, &request->parameters.poll_cq, &scratch, result);
    else
        SetInvalid(result);
}
NDS_EXPORT_AIV_META_INFO(NdsAivConnectionOp);
