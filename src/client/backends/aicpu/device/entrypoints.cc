#include "api.h"
#include "internal.h"
#include "launch_args.hh"

#include <stdint.h>

namespace {
constexpr uint32_t kEntryInvalidArgument = 1U;

void entry_barrier() {
#if defined(__aarch64__)
    asm volatile("dsb st" ::: "memory");
#else
    asm volatile("" ::: "memory");
#endif
}

void set_invalid(int32_t *return_value) {
    NdsAicpuSetReturnValue(return_value, NDS_OPERATION_INVALID_ARGUMENT);
}

template <typename Request>
bool valid_verbs_request(const Request *request) {
    return request != nullptr;
}

template <typename Request>
bool valid_rdma_request(const Request *request) {
    return request != nullptr;
}

template <typename Args>
bool valid_storage_args(const Args *request) {
    return request != nullptr;
}

template <typename Args>
Args *device_args(void *launch_args) {
    return launch_args == nullptr ? nullptr : &static_cast<NdsAicpuLaunchArgs<Args> *>(launch_args)->request;
}

template <typename Args>
int32_t *launch_return_value(void *launch_args) {
    if (launch_args == nullptr)
        return nullptr;
    const auto *args = static_cast<const NdsAicpuLaunchArgs<Args> *>(launch_args);
    return reinterpret_cast<int32_t *>(args->return_value_address);
}
}  // namespace

extern "C" __attribute__((visibility("default"))) uint32_t nds_aicpu_post_send_kernel(void *args) {
    auto *request = device_args<NdsPostSendArgs>(args);
    if (!valid_verbs_request(request)) {
        set_invalid(launch_return_value<NdsPostSendArgs>(args));
        return kEntryInvalidArgument;
    }
    const uint32_t status = nds_aicpu_post_send(&request->qp, &request->wr, launch_return_value<NdsPostSendArgs>(args));
    entry_barrier();
    return status;
}
extern "C" __attribute__((visibility("default"))) uint32_t nds_aicpu_post_recv_kernel(void *args) {
    auto *request = device_args<NdsPostRecvArgs>(args);
    if (!valid_verbs_request(request)) {
        set_invalid(launch_return_value<NdsPostRecvArgs>(args));
        return kEntryInvalidArgument;
    }
    const uint32_t status = nds_aicpu_post_recv(&request->qp, &request->wr, launch_return_value<NdsPostRecvArgs>(args));
    entry_barrier();
    return status;
}
extern "C" __attribute__((visibility("default"))) uint32_t nds_aicpu_poll_cq_kernel(void *args) {
    auto *request = device_args<NdsPollCqArgs>(args);
    if (!valid_verbs_request(request)) {
        set_invalid(launch_return_value<NdsPollCqArgs>(args));
        return kEntryInvalidArgument;
    }
    const uint32_t status =
        nds_aicpu_poll_cq(&request->qp, request->is_send_cq, request->max_completions,
                          reinterpret_cast<NdsWc *>(request->wc_address), launch_return_value<NdsPollCqArgs>(args));
    entry_barrier();
    return status;
}
extern "C" __attribute__((visibility("default"))) uint32_t nds_aicpu_rdma_send_kernel(void *args) {
    auto *request = device_args<NdsRdmaSendArgs>(args);
    if (!valid_rdma_request(request)) {
        set_invalid(launch_return_value<NdsRdmaSendArgs>(args));
        return kEntryInvalidArgument;
    }
    const uint32_t status = nds_aicpu_rdma_send(&request->transport, request->queue_index, &request->wr,
                                                launch_return_value<NdsRdmaSendArgs>(args));
    entry_barrier();
    return status;
}

extern "C" __attribute__((visibility("default"))) uint32_t nds_aicpu_rdma_recv_kernel(void *args) {
    auto *request = device_args<NdsRdmaRecvArgs>(args);
    if (!valid_rdma_request(request)) {
        set_invalid(launch_return_value<NdsRdmaRecvArgs>(args));
        return kEntryInvalidArgument;
    }
    const uint32_t status = nds_aicpu_rdma_recv(&request->transport, request->queue_index, &request->wr,
                                                launch_return_value<NdsRdmaRecvArgs>(args));
    entry_barrier();
    return status;
}

extern "C" __attribute__((visibility("default"))) uint32_t nds_aicpu_rdma_read_kernel(void *args) {
    auto *request = device_args<NdsRdmaReadArgs>(args);
    if (!valid_rdma_request(request)) {
        set_invalid(launch_return_value<NdsRdmaReadArgs>(args));
        return kEntryInvalidArgument;
    }
    const uint32_t status = nds_aicpu_rdma_read(&request->transport, request->queue_index, &request->wr,
                                                launch_return_value<NdsRdmaReadArgs>(args));
    entry_barrier();
    return status;
}

extern "C" __attribute__((visibility("default"))) uint32_t nds_aicpu_rdma_write_kernel(void *args) {
    auto *request = device_args<NdsRdmaWriteArgs>(args);
    if (!valid_rdma_request(request)) {
        set_invalid(launch_return_value<NdsRdmaWriteArgs>(args));
        return kEntryInvalidArgument;
    }
    const uint32_t status = nds_aicpu_rdma_write(&request->transport, request->queue_index, &request->wr,
                                                 launch_return_value<NdsRdmaWriteArgs>(args));
    entry_barrier();
    return status;
}

extern "C" __attribute__((visibility("default"))) uint32_t nds_aicpu_storage_bootstrap_kernel(void *args) {
    auto *request = device_args<NdsStorageBootstrapArgs>(args);
    if (!valid_storage_args(request)) {
        set_invalid(launch_return_value<NdsStorageBootstrapArgs>(args));
        return kEntryInvalidArgument;
    }
    const uint32_t status =
        nds_aicpu_storage_bootstrap(&request->bootstrap, launch_return_value<NdsStorageBootstrapArgs>(args));
    entry_barrier();
    return status;
}

extern "C" __attribute__((visibility("default"))) uint32_t nds_aicpu_storage_read_kernel(void *args) {
    auto *request = device_args<NdsStorageOperationArgs>(args);
    if (!valid_storage_args(request)) {
        set_invalid(launch_return_value<NdsStorageOperationArgs>(args));
        return kEntryInvalidArgument;
    }
    const uint32_t status = nds_aicpu_storage_read(request, launch_return_value<NdsStorageOperationArgs>(args));
    entry_barrier();
    return status;
}

extern "C" __attribute__((visibility("default"))) uint32_t nds_aicpu_storage_write_kernel(void *args) {
    auto *request = device_args<NdsStorageOperationArgs>(args);
    if (!valid_storage_args(request)) {
        set_invalid(launch_return_value<NdsStorageOperationArgs>(args));
        return kEntryInvalidArgument;
    }
    const uint32_t status = nds_aicpu_storage_write(request, launch_return_value<NdsStorageOperationArgs>(args));
    entry_barrier();
    return status;
}

extern "C" __attribute__((visibility("default"))) uint32_t nds_aicpu_storage_batch_read_kernel(void *args) {
    auto *request = device_args<NdsStorageBatchOperationArgs>(args);
    if (!valid_storage_args(request)) {
        set_invalid(launch_return_value<NdsStorageBatchOperationArgs>(args));
        return kEntryInvalidArgument;
    }
    const uint32_t status =
        nds_aicpu_storage_batch_read(request, launch_return_value<NdsStorageBatchOperationArgs>(args));
    entry_barrier();
    return status;
}

extern "C" __attribute__((visibility("default"))) uint32_t nds_aicpu_storage_batch_write_kernel(void *args) {
    auto *request = device_args<NdsStorageBatchOperationArgs>(args);
    if (!valid_storage_args(request)) {
        set_invalid(launch_return_value<NdsStorageBatchOperationArgs>(args));
        return kEntryInvalidArgument;
    }
    const uint32_t status =
        nds_aicpu_storage_batch_write(request, launch_return_value<NdsStorageBatchOperationArgs>(args));
    entry_barrier();
    return status;
}

extern "C" __attribute__((visibility("default"))) uint32_t nds_aicpu_storage_wait_kernel(void *args) {
    auto *request = device_args<NdsStorageWaitArgs>(args);
    if (!valid_storage_args(request) || !nds_storage_wait_valid(&request->storage, request->slot_id)) {
        set_invalid(launch_return_value<NdsStorageWaitArgs>(args));
        return kEntryInvalidArgument;
    }
    const uint32_t status =
        nds_aicpu_storage_wait(&request->storage, request->slot_id, launch_return_value<NdsStorageWaitArgs>(args));
    entry_barrier();
    return status;
}
