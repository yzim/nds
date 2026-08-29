#include "launcher.hh"

#include "kernel_operator.h"

extern "C" __global__ __aicore__ void nds_aiv_post_send_kernel(GM_ADDR qp_address, GM_ADDR wr_address,
                                                               GM_ADDR return_value);
extern "C" __global__ __aicore__ void nds_aiv_post_recv_kernel(GM_ADDR qp_address, GM_ADDR wr_address,
                                                               GM_ADDR return_value);
extern "C" __global__ __aicore__ void nds_aiv_poll_cq_kernel(GM_ADDR qp_address, uint32_t is_send_cq,
                                                             uint32_t max_completions, GM_ADDR wc_address,
                                                             GM_ADDR return_value);
extern "C" __global__ __aicore__ void nds_aiv_rdma_send_kernel(GM_ADDR transport_address, GM_ADDR wr_address,
                                                               GM_ADDR return_value);
extern "C" __global__ __aicore__ void nds_aiv_rdma_recv_kernel(GM_ADDR transport_address, GM_ADDR wr_address,
                                                               GM_ADDR return_value);
extern "C" __global__ __aicore__ void nds_aiv_rdma_read_kernel(GM_ADDR transport_address, GM_ADDR wr_address,
                                                               GM_ADDR return_value);
extern "C" __global__ __aicore__ void nds_aiv_rdma_write_kernel(GM_ADDR transport_address, GM_ADDR wr_address,
                                                                GM_ADDR return_value);
extern "C" __global__ __aicore__ void nds_aiv_storage_read_kernel(GM_ADDR context_address, GM_ADDR command_address,
                                                                  GM_ADDR return_value);
extern "C" __global__ __aicore__ void nds_aiv_storage_write_kernel(GM_ADDR context_address, GM_ADDR command_address,
                                                                   GM_ADDR return_value);
extern "C" __global__ __aicore__ void nds_aiv_storage_batch_read_kernel(GM_ADDR context_address,
                                                                        GM_ADDR command_address, GM_ADDR return_value);
extern "C" __global__ __aicore__ void nds_aiv_storage_batch_write_kernel(GM_ADDR context_address,
                                                                         GM_ADDR command_address, GM_ADDR return_value);
extern "C" __global__ __aicore__ void nds_aiv_storage_wait_kernel(GM_ADDR context_address, uint64_t command_id,
                                                                  uint64_t expected_bytes, GM_ADDR return_value);

namespace {

template <typename KernelLaunch>
int launch(aclrtStream stream, KernelLaunch &&kernel_launch) {
    if (stream == nullptr)
        return ACL_ERROR_INVALID_PARAM;
    kernel_launch();
    return aclrtSynchronizeStream(stream);
}

}  // namespace

extern "C" int nds_aiv_host_post_send(aclrtStream stream, NdsDevicePostSendArgs *request) {
    if (request == nullptr)
        return ACL_ERROR_INVALID_PARAM;
    return launch(stream, [&] {
        nds_aiv_post_send_kernel<<<1, nullptr, stream>>>(reinterpret_cast<GM_ADDR>(&request->qp),
                                                         reinterpret_cast<GM_ADDR>(&request->wr),
                                                         reinterpret_cast<GM_ADDR>(&request->return_value));
    });
}

extern "C" int nds_aiv_host_post_recv(aclrtStream stream, NdsDevicePostRecvArgs *request) {
    if (request == nullptr)
        return ACL_ERROR_INVALID_PARAM;
    return launch(stream, [&] {
        nds_aiv_post_recv_kernel<<<1, nullptr, stream>>>(reinterpret_cast<GM_ADDR>(&request->qp),
                                                         reinterpret_cast<GM_ADDR>(&request->wr),
                                                         reinterpret_cast<GM_ADDR>(&request->return_value));
    });
}

extern "C" int nds_aiv_host_poll_cq(aclrtStream stream, NdsDevicePollCqArgs *request) {
    if (request == nullptr)
        return ACL_ERROR_INVALID_PARAM;
    return launch(stream, [&] {
        nds_aiv_poll_cq_kernel<<<1, nullptr, stream>>>(
            reinterpret_cast<GM_ADDR>(&request->qp), request->is_send_cq, request->max_completions,
            reinterpret_cast<GM_ADDR>(request->wc_address), reinterpret_cast<GM_ADDR>(&request->return_value));
    });
}

extern "C" int nds_aiv_host_rdma_send(aclrtStream stream, NdsDeviceRdmaSendArgs *request) {
    if (request == nullptr)
        return ACL_ERROR_INVALID_PARAM;
    return launch(stream, [&] {
        nds_aiv_rdma_send_kernel<<<1, nullptr, stream>>>(reinterpret_cast<GM_ADDR>(&request->transport),
                                                         reinterpret_cast<GM_ADDR>(&request->wr),
                                                         reinterpret_cast<GM_ADDR>(&request->return_value));
    });
}

extern "C" int nds_aiv_host_rdma_recv(aclrtStream stream, NdsDeviceRdmaRecvArgs *request) {
    if (request == nullptr)
        return ACL_ERROR_INVALID_PARAM;
    return launch(stream, [&] {
        nds_aiv_rdma_recv_kernel<<<1, nullptr, stream>>>(reinterpret_cast<GM_ADDR>(&request->transport),
                                                         reinterpret_cast<GM_ADDR>(&request->wr),
                                                         reinterpret_cast<GM_ADDR>(&request->return_value));
    });
}

extern "C" int nds_aiv_host_rdma_read(aclrtStream stream, NdsDeviceRdmaReadArgs *request) {
    if (request == nullptr)
        return ACL_ERROR_INVALID_PARAM;
    return launch(stream, [&] {
        nds_aiv_rdma_read_kernel<<<1, nullptr, stream>>>(reinterpret_cast<GM_ADDR>(&request->transport),
                                                         reinterpret_cast<GM_ADDR>(&request->wr),
                                                         reinterpret_cast<GM_ADDR>(&request->return_value));
    });
}

extern "C" int nds_aiv_host_rdma_write(aclrtStream stream, NdsDeviceRdmaWriteArgs *request) {
    if (request == nullptr)
        return ACL_ERROR_INVALID_PARAM;
    return launch(stream, [&] {
        nds_aiv_rdma_write_kernel<<<1, nullptr, stream>>>(reinterpret_cast<GM_ADDR>(&request->transport),
                                                          reinterpret_cast<GM_ADDR>(&request->wr),
                                                          reinterpret_cast<GM_ADDR>(&request->return_value));
    });
}

extern "C" int nds_aiv_host_storage_read(aclrtStream stream, NdsDeviceStorageReadArgs *request) {
    if (request == nullptr)
        return ACL_ERROR_INVALID_PARAM;
    return launch(stream, [&] {
        nds_aiv_storage_read_kernel<<<1, nullptr, stream>>>(reinterpret_cast<GM_ADDR>(&request->context),
                                                            reinterpret_cast<GM_ADDR>(&request->command),
                                                            reinterpret_cast<GM_ADDR>(&request->return_value));
    });
}

extern "C" int nds_aiv_host_storage_write(aclrtStream stream, NdsDeviceStorageWriteArgs *request) {
    if (request == nullptr)
        return ACL_ERROR_INVALID_PARAM;
    return launch(stream, [&] {
        nds_aiv_storage_write_kernel<<<1, nullptr, stream>>>(reinterpret_cast<GM_ADDR>(&request->context),
                                                             reinterpret_cast<GM_ADDR>(&request->command),
                                                             reinterpret_cast<GM_ADDR>(&request->return_value));
    });
}

extern "C" int nds_aiv_host_storage_batch_read(aclrtStream stream, NdsDeviceStorageBatchReadArgs *request) {
    if (request == nullptr)
        return ACL_ERROR_INVALID_PARAM;
    return launch(stream, [&] {
        nds_aiv_storage_batch_read_kernel<<<1, nullptr, stream>>>(reinterpret_cast<GM_ADDR>(&request->context),
                                                                  reinterpret_cast<GM_ADDR>(&request->command),
                                                                  reinterpret_cast<GM_ADDR>(&request->return_value));
    });
}

extern "C" int nds_aiv_host_storage_batch_write(aclrtStream stream, NdsDeviceStorageBatchWriteArgs *request) {
    if (request == nullptr)
        return ACL_ERROR_INVALID_PARAM;
    return launch(stream, [&] {
        nds_aiv_storage_batch_write_kernel<<<1, nullptr, stream>>>(reinterpret_cast<GM_ADDR>(&request->context),
                                                                   reinterpret_cast<GM_ADDR>(&request->command),
                                                                   reinterpret_cast<GM_ADDR>(&request->return_value));
    });
}

extern "C" int nds_aiv_host_storage_wait(aclrtStream stream, NdsDeviceStorageWaitArgs *request) {
    if (request == nullptr)
        return ACL_ERROR_INVALID_PARAM;
    return launch(stream, [&] {
        nds_aiv_storage_wait_kernel<<<1, nullptr, stream>>>(reinterpret_cast<GM_ADDR>(&request->context),
                                                            request->command_id, request->expected_bytes,
                                                            reinterpret_cast<GM_ADDR>(&request->return_value));
    });
}
