#include "launcher.hh"

#include <cstdint>

namespace {

int launch(aclrtFuncHandle function, aclrtStream stream, const void *request) {
    if (function == nullptr || stream == nullptr || request == nullptr)
        return ACL_ERROR_INVALID_PARAM;
    const std::uint64_t request_address = reinterpret_cast<std::uint64_t>(request);
    const int result = aclrtLaunchKernelWithHostArgs(function, 1U, stream, nullptr, &request_address,
                                                     sizeof(request_address), nullptr, 0U);
    if (result != ACL_SUCCESS)
        return result;
    return aclrtSynchronizeStream(stream);
}

}  // namespace

extern "C" int nds_aicpu_host_post_send(aclrtFuncHandle function, aclrtStream stream, NdsDevicePostSendArgs *request) {
    return launch(function, stream, request);
}

extern "C" int nds_aicpu_host_post_recv(aclrtFuncHandle function, aclrtStream stream, NdsDevicePostRecvArgs *request) {
    return launch(function, stream, request);
}

extern "C" int nds_aicpu_host_poll_cq(aclrtFuncHandle function, aclrtStream stream, NdsDevicePollCqArgs *request) {
    return launch(function, stream, request);
}

extern "C" int nds_aicpu_host_rdma_send(aclrtFuncHandle function, aclrtStream stream, NdsDeviceRdmaSendArgs *request) {
    return launch(function, stream, request);
}

extern "C" int nds_aicpu_host_rdma_recv(aclrtFuncHandle function, aclrtStream stream, NdsDeviceRdmaRecvArgs *request) {
    return launch(function, stream, request);
}

extern "C" int nds_aicpu_host_rdma_read(aclrtFuncHandle function, aclrtStream stream, NdsDeviceRdmaReadArgs *request) {
    return launch(function, stream, request);
}

extern "C" int nds_aicpu_host_rdma_write(aclrtFuncHandle function, aclrtStream stream,
                                         NdsDeviceRdmaWriteArgs *request) {
    return launch(function, stream, request);
}

extern "C" int nds_aicpu_host_storage_read(aclrtFuncHandle function, aclrtStream stream,
                                           NdsDeviceStorageReadArgs *request) {
    return launch(function, stream, request);
}

extern "C" int nds_aicpu_host_storage_write(aclrtFuncHandle function, aclrtStream stream,
                                            NdsDeviceStorageWriteArgs *request) {
    return launch(function, stream, request);
}

extern "C" int nds_aicpu_host_storage_batch_read(aclrtFuncHandle function, aclrtStream stream,
                                                 NdsDeviceStorageBatchReadArgs *request) {
    return launch(function, stream, request);
}

extern "C" int nds_aicpu_host_storage_batch_write(aclrtFuncHandle function, aclrtStream stream,
                                                  NdsDeviceStorageBatchWriteArgs *request) {
    return launch(function, stream, request);
}

extern "C" int nds_aicpu_host_storage_wait(aclrtFuncHandle function, aclrtStream stream,
                                           NdsDeviceStorageWaitArgs *request) {
    return launch(function, stream, request);
}
