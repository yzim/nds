#include "nds_aicpu_device_api.h"

#include <stdint.h>

namespace {
constexpr uint32_t kEntrySuccess = 0U;
constexpr uint32_t kEntryInvalidArgument = 1U;

void entry_barrier() {
#if defined(__aarch64__)
    asm volatile("dsb st" ::: "memory");
#else
    asm volatile("" ::: "memory");
#endif
}
}  // namespace

extern "C" __attribute__((visibility("default"))) uint32_t NdsAicpuConnectionOp(void *args) {
    auto *request = static_cast<nds_device_operation_request *>(args);
    if (request == nullptr || request->abi_version != NDS_DEVICE_OPERATIONS_ABI_VERSION ||
        request->size != sizeof(*request) || request->operation_result_address == 0U ||
        request->connection.abi_version != NDS_DEVICE_CONNECTION_ABI_VERSION)
        return kEntryInvalidArgument;
    auto *result = reinterpret_cast<nds_device_operation_result *>(request->operation_result_address);
    uint32_t dispatch = kEntryInvalidArgument;
    if (request->operation == NDS_DEVICE_RDMA_SEND)
        dispatch = NdsAicpuRdmaSend(&request->connection, &request->parameters.transfer, result);
    else if (request->operation == NDS_DEVICE_RDMA_RECV)
        dispatch = NdsAicpuRdmaRecv(&request->connection, &request->parameters.transfer, result);
    else if (request->operation == NDS_DEVICE_RDMA_READ)
        dispatch = NdsAicpuRdmaRead(&request->connection, &request->parameters.transfer, result);
    else if (request->operation == NDS_DEVICE_RDMA_WRITE)
        dispatch = NdsAicpuRdmaWrite(&request->connection, &request->parameters.transfer, result);
    else if (request->operation == NDS_DEVICE_POLL_CQ)
        dispatch = NdsAicpuPollCq(&request->connection.qp, &request->parameters.poll_cq, result);
    if (dispatch != kEntrySuccess) {
        result->status = NDS_DEVICE_OPERATION_INVALID_ARGUMENT;
        result->path = NDS_DEVICE_OPERATION_PATH_NONE;
        result->provider_result = 0;
        result->reserved = 0U;
    }
    entry_barrier();
    return dispatch;
}
