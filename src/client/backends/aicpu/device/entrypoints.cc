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
    NdsAicpuSetReturnValue(return_value, NDS_DEVICE_OPERATION_INVALID_ARGUMENT);
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
    auto *request = device_args<NdsDevicePostSendArgs>(args);
    if (!valid_verbs_request(request)) {
        set_invalid(launch_return_value<NdsDevicePostSendArgs>(args));
        return kEntryInvalidArgument;
    }
    const uint32_t status =
        nds_aicpu_post_send(&request->qp, &request->wr, launch_return_value<NdsDevicePostSendArgs>(args));
    entry_barrier();
    return status;
}
extern "C" __attribute__((visibility("default"))) uint32_t nds_aicpu_post_recv_kernel(void *args) {
    auto *request = device_args<NdsDevicePostRecvArgs>(args);
    if (!valid_verbs_request(request)) {
        set_invalid(launch_return_value<NdsDevicePostRecvArgs>(args));
        return kEntryInvalidArgument;
    }
    const uint32_t status =
        nds_aicpu_post_recv(&request->qp, &request->wr, launch_return_value<NdsDevicePostRecvArgs>(args));
    entry_barrier();
    return status;
}
extern "C" __attribute__((visibility("default"))) uint32_t nds_aicpu_poll_cq_kernel(void *args) {
    auto *request = device_args<NdsDevicePollCqArgs>(args);
    if (!valid_verbs_request(request)) {
        set_invalid(launch_return_value<NdsDevicePollCqArgs>(args));
        return kEntryInvalidArgument;
    }
    const uint32_t status = nds_aicpu_poll_cq(&request->qp, request->is_send_cq, request->max_completions,
                                              reinterpret_cast<NdsDeviceWc *>(request->wc_address),
                                              launch_return_value<NdsDevicePollCqArgs>(args));
    entry_barrier();
    return status;
}
extern "C" __attribute__((visibility("default"))) uint32_t nds_aicpu_rdma_send_kernel(void *args) {
    auto *request = device_args<NdsDeviceRdmaSendArgs>(args);
    if (!valid_rdma_request(request)) {
        set_invalid(launch_return_value<NdsDeviceRdmaSendArgs>(args));
        return kEntryInvalidArgument;
    }
    const uint32_t status =
        nds_aicpu_rdma_send(&request->transport, &request->wr, launch_return_value<NdsDeviceRdmaSendArgs>(args));
    entry_barrier();
    return status;
}

extern "C" __attribute__((visibility("default"))) uint32_t nds_aicpu_rdma_recv_kernel(void *args) {
    auto *request = device_args<NdsDeviceRdmaRecvArgs>(args);
    if (!valid_rdma_request(request)) {
        set_invalid(launch_return_value<NdsDeviceRdmaRecvArgs>(args));
        return kEntryInvalidArgument;
    }
    const uint32_t status =
        nds_aicpu_rdma_recv(&request->transport, &request->wr, launch_return_value<NdsDeviceRdmaRecvArgs>(args));
    entry_barrier();
    return status;
}

extern "C" __attribute__((visibility("default"))) uint32_t nds_aicpu_rdma_read_kernel(void *args) {
    auto *request = device_args<NdsDeviceRdmaReadArgs>(args);
    if (!valid_rdma_request(request)) {
        set_invalid(launch_return_value<NdsDeviceRdmaReadArgs>(args));
        return kEntryInvalidArgument;
    }
    const uint32_t status =
        nds_aicpu_rdma_read(&request->transport, &request->wr, launch_return_value<NdsDeviceRdmaReadArgs>(args));
    entry_barrier();
    return status;
}

extern "C" __attribute__((visibility("default"))) uint32_t nds_aicpu_rdma_write_kernel(void *args) {
    auto *request = device_args<NdsDeviceRdmaWriteArgs>(args);
    if (!valid_rdma_request(request)) {
        set_invalid(launch_return_value<NdsDeviceRdmaWriteArgs>(args));
        return kEntryInvalidArgument;
    }
    const uint32_t status =
        nds_aicpu_rdma_write(&request->transport, &request->wr, launch_return_value<NdsDeviceRdmaWriteArgs>(args));
    entry_barrier();
    return status;
}

extern "C" __attribute__((visibility("default"))) uint32_t nds_aicpu_storage_read_kernel(void *args) {
    auto *request = device_args<NdsDeviceStorageReadArgs>(args);
    if (!valid_storage_args(request)) {
        set_invalid(launch_return_value<NdsDeviceStorageReadArgs>(args));
        return kEntryInvalidArgument;
    }
    const uint32_t status = nds_aicpu_storage_read(&request->context, &request->command,
                                                   launch_return_value<NdsDeviceStorageReadArgs>(args));
    entry_barrier();
    return status;
}

extern "C" __attribute__((visibility("default"))) uint32_t nds_aicpu_storage_write_kernel(void *args) {
    auto *request = device_args<NdsDeviceStorageWriteArgs>(args);
    if (!valid_storage_args(request)) {
        set_invalid(launch_return_value<NdsDeviceStorageWriteArgs>(args));
        return kEntryInvalidArgument;
    }
    const uint32_t status = nds_aicpu_storage_write(&request->context, &request->command,
                                                    launch_return_value<NdsDeviceStorageWriteArgs>(args));
    entry_barrier();
    return status;
}

extern "C" __attribute__((visibility("default"))) uint32_t nds_aicpu_storage_batch_read_kernel(void *args) {
    auto *request = device_args<NdsDeviceStorageBatchReadArgs>(args);
    if (!valid_storage_args(request)) {
        set_invalid(launch_return_value<NdsDeviceStorageBatchReadArgs>(args));
        return kEntryInvalidArgument;
    }
    const uint32_t status = nds_aicpu_storage_batch_read(&request->context, &request->command,
                                                         launch_return_value<NdsDeviceStorageBatchReadArgs>(args));
    entry_barrier();
    return status;
}

extern "C" __attribute__((visibility("default"))) uint32_t nds_aicpu_storage_batch_write_kernel(void *args) {
    auto *request = device_args<NdsDeviceStorageBatchWriteArgs>(args);
    if (!valid_storage_args(request)) {
        set_invalid(launch_return_value<NdsDeviceStorageBatchWriteArgs>(args));
        return kEntryInvalidArgument;
    }
    const uint32_t status = nds_aicpu_storage_batch_write(&request->context, &request->command,
                                                          launch_return_value<NdsDeviceStorageBatchWriteArgs>(args));
    entry_barrier();
    return status;
}

extern "C" __attribute__((visibility("default"))) uint32_t nds_aicpu_storage_wait_kernel(void *args) {
    auto *request = device_args<NdsDeviceStorageWaitArgs>(args);
    if (!valid_storage_args(request) ||
        !nds_device_storage_wait_valid(&request->context, request->command_id, request->expected_bytes)) {
        set_invalid(launch_return_value<NdsDeviceStorageWaitArgs>(args));
        return kEntryInvalidArgument;
    }
    const uint32_t status = nds_aicpu_storage_wait(&request->context, request->command_id, request->expected_bytes,
                                                   launch_return_value<NdsDeviceStorageWaitArgs>(args));
    entry_barrier();
    return status;
}
