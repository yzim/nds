#include "api.h"

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
}  // namespace

extern "C" __attribute__((visibility("default"))) uint32_t NdsAicpuPostSend(void *args) {
    auto *request = static_cast<NdsDevicePostSendArgs *>(args);
    if (!valid_verbs_request(request)) {
        set_invalid(request == nullptr ? nullptr : &request->return_value);
        return kEntryInvalidArgument;
    }
    const uint32_t status = NdsAicpuPostSendImpl(&request->qp, &request->wr, &request->return_value);
    entry_barrier();
    return status;
}
extern "C" __attribute__((visibility("default"))) uint32_t NdsAicpuPostRecv(void *args) {
    auto *request = static_cast<NdsDevicePostRecvArgs *>(args);
    if (!valid_verbs_request(request)) {
        set_invalid(request == nullptr ? nullptr : &request->return_value);
        return kEntryInvalidArgument;
    }
    const uint32_t status = NdsAicpuPostRecvImpl(&request->qp, &request->wr, &request->return_value);
    entry_barrier();
    return status;
}
extern "C" __attribute__((visibility("default"))) uint32_t NdsAicpuPollCq(void *args) {
    auto *request = static_cast<NdsDevicePollCqArgs *>(args);
    if (!valid_verbs_request(request)) {
        set_invalid(request == nullptr ? nullptr : &request->return_value);
        return kEntryInvalidArgument;
    }
    const uint32_t status = NdsAicpuPollCqImpl(
        &request->qp, request->is_send_cq, request->max_completions,
        reinterpret_cast<NdsDeviceWc *>(request->wc_address), &request->return_value);
    entry_barrier();
    return status;
}
extern "C" __attribute__((visibility("default"))) uint32_t NdsAicpuRdmaSend(void *args) {
    auto *request = static_cast<NdsDeviceRdmaSendArgs *>(args);
    if (!valid_rdma_request(request)) {
        set_invalid(request == nullptr ? nullptr : &request->return_value);
        return kEntryInvalidArgument;
    }
    const uint32_t status = NdsAicpuRdmaSendImpl(&request->transport, &request->wr, &request->return_value);
    entry_barrier();
    return status;
}

extern "C" __attribute__((visibility("default"))) uint32_t NdsAicpuRdmaRecv(void *args) {
    auto *request = static_cast<NdsDeviceRdmaRecvArgs *>(args);
    if (!valid_rdma_request(request)) {
        set_invalid(request == nullptr ? nullptr : &request->return_value);
        return kEntryInvalidArgument;
    }
    const uint32_t status = NdsAicpuRdmaRecvImpl(&request->transport, &request->wr, &request->return_value);
    entry_barrier();
    return status;
}

extern "C" __attribute__((visibility("default"))) uint32_t NdsAicpuRdmaRead(void *args) {
    auto *request = static_cast<NdsDeviceRdmaReadArgs *>(args);
    if (!valid_rdma_request(request)) {
        set_invalid(request == nullptr ? nullptr : &request->return_value);
        return kEntryInvalidArgument;
    }
    const uint32_t status = NdsAicpuRdmaReadImpl(&request->transport, &request->wr, &request->return_value);
    entry_barrier();
    return status;
}

extern "C" __attribute__((visibility("default"))) uint32_t NdsAicpuRdmaWrite(void *args) {
    auto *request = static_cast<NdsDeviceRdmaWriteArgs *>(args);
    if (!valid_rdma_request(request)) {
        set_invalid(request == nullptr ? nullptr : &request->return_value);
        return kEntryInvalidArgument;
    }
    const uint32_t status = NdsAicpuRdmaWriteImpl(&request->transport, &request->wr, &request->return_value);
    entry_barrier();
    return status;
}

extern "C" __attribute__((visibility("default"))) uint32_t NdsAicpuStorageRead(void *args) {
    auto *request = static_cast<NdsDeviceStorageReadArgs *>(args);
    if (!valid_storage_args(request)) {
        set_invalid(request == nullptr ? nullptr : &request->return_value);
        return kEntryInvalidArgument;
    }
    const uint32_t status = NdsAicpuStorageReadImpl(&request->context, &request->command, &request->return_value);
    entry_barrier();
    return status;
}

extern "C" __attribute__((visibility("default"))) uint32_t NdsAicpuStorageWrite(void *args) {
    auto *request = static_cast<NdsDeviceStorageWriteArgs *>(args);
    if (!valid_storage_args(request)) {
        set_invalid(request == nullptr ? nullptr : &request->return_value);
        return kEntryInvalidArgument;
    }
    const uint32_t status = NdsAicpuStorageWriteImpl(&request->context, &request->command, &request->return_value);
    entry_barrier();
    return status;
}

extern "C" __attribute__((visibility("default"))) uint32_t NdsAicpuStorageBatchRead(void *args) {
    auto *request = static_cast<NdsDeviceStorageBatchReadArgs *>(args);
    if (!valid_storage_args(request)) {
        set_invalid(request == nullptr ? nullptr : &request->return_value);
        return kEntryInvalidArgument;
    }
    const uint32_t status =
        NdsAicpuStorageBatchReadImpl(&request->context, &request->command, &request->return_value);
    entry_barrier();
    return status;
}

extern "C" __attribute__((visibility("default"))) uint32_t NdsAicpuStorageBatchWrite(void *args) {
    auto *request = static_cast<NdsDeviceStorageBatchWriteArgs *>(args);
    if (!valid_storage_args(request)) {
        set_invalid(request == nullptr ? nullptr : &request->return_value);
        return kEntryInvalidArgument;
    }
    const uint32_t status =
        NdsAicpuStorageBatchWriteImpl(&request->context, &request->command, &request->return_value);
    entry_barrier();
    return status;
}

extern "C" __attribute__((visibility("default"))) uint32_t NdsAicpuStorageWait(void *args) {
    auto *request = static_cast<NdsDeviceStorageWaitArgs *>(args);
    if (!valid_storage_args(request) ||
        !nds_device_storage_wait_valid(&request->context, request->command_id, request->expected_bytes)) {
        set_invalid(request == nullptr ? nullptr : &request->return_value);
        return kEntryInvalidArgument;
    }
    const uint32_t status =
        NdsAicpuStorageWaitImpl(&request->context, request->command_id, request->expected_bytes, &request->return_value);
    entry_barrier();
    return status;
}
