#include "api.h"

#include "nds/device_operator_args.h"

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

void set_invalid(nds_device_operation_result *result) {
    if (result == nullptr) return;
    result->status = NDS_DEVICE_OPERATION_INVALID_ARGUMENT;
    result->path = NDS_DEVICE_OPERATION_PATH_NONE;
    result->provider_result = 0;
    result->reserved = 0U;
}

template <typename Request>
bool valid_request(const Request *request, uint64_t result_address) {
    return request != nullptr && request->abi_version == NDS_DEVICE_OPERATOR_ARGS_ABI_VERSION &&
           request->size == sizeof(*request) && result_address != 0U;
}

bool valid_operation_request(const nds_device_operation_request *request) {
    return request != nullptr && request->abi_version == NDS_DEVICE_OPERATIONS_ABI_VERSION &&
           request->size == sizeof(*request) && request->operation_result_address != 0U &&
           request->connection.abi_version == NDS_DEVICE_CONNECTION_ABI_VERSION;
}

bool valid_storage_request(const nds_device_storage_request *request) {
    return request != nullptr && request->abi_version == NDS_DEVICE_STORAGE_ABI_VERSION &&
           request->size == sizeof(*request) && request->operation_result_address != 0U;
}
}  // namespace

extern "C" __attribute__((visibility("default"))) uint32_t NdsAicpuPostSend(void *args) {
    auto *request = static_cast<nds_device_post_send_request *>(args);
    auto *result = request == nullptr ? nullptr : reinterpret_cast<nds_device_operation_result *>(request->operation_result_address);
    if (!valid_request(request, request == nullptr ? 0U : request->operation_result_address)) { set_invalid(result); return kEntryInvalidArgument; }
    const uint32_t status = NdsAicpuPostSendImpl(&request->qp, &request->wr, result); entry_barrier(); return status;
}
extern "C" __attribute__((visibility("default"))) uint32_t NdsAicpuPostRecv(void *args) {
    auto *request = static_cast<nds_device_post_recv_request *>(args);
    auto *result = request == nullptr ? nullptr : reinterpret_cast<nds_device_operation_result *>(request->operation_result_address);
    if (!valid_request(request, request == nullptr ? 0U : request->operation_result_address)) { set_invalid(result); return kEntryInvalidArgument; }
    const uint32_t status = NdsAicpuPostRecvImpl(&request->qp, &request->wr, result); entry_barrier(); return status;
}
extern "C" __attribute__((visibility("default"))) uint32_t NdsAicpuPollCq(void *args) {
    auto *request = static_cast<nds_device_operation_request *>(args);
    auto *result = request == nullptr ? nullptr : reinterpret_cast<nds_device_operation_result *>(request->operation_result_address);
    if (!valid_operation_request(request)) { set_invalid(result); return kEntryInvalidArgument; }
    const uint32_t status = NdsAicpuPollCqImpl(&request->connection.qp, &request->parameters.poll_cq, result); entry_barrier(); return status;
}
extern "C" __attribute__((visibility("default"))) uint32_t NdsAicpuRdmaSend(void *args) {
    auto *request = static_cast<nds_device_operation_request *>(args);
    auto *result = request == nullptr ? nullptr : reinterpret_cast<nds_device_operation_result *>(request->operation_result_address);
    if (!valid_operation_request(request)) { set_invalid(result); return kEntryInvalidArgument; }
    const uint32_t status = NdsAicpuRdmaSendImpl(&request->connection, &request->parameters.transfer, result);
    entry_barrier();
    return status;
}

extern "C" __attribute__((visibility("default"))) uint32_t NdsAicpuRdmaRecv(void *args) {
    auto *request = static_cast<nds_device_operation_request *>(args);
    auto *result = request == nullptr ? nullptr : reinterpret_cast<nds_device_operation_result *>(request->operation_result_address);
    if (!valid_operation_request(request)) { set_invalid(result); return kEntryInvalidArgument; }
    const uint32_t status = NdsAicpuRdmaRecvImpl(&request->connection, &request->parameters.transfer, result);
    entry_barrier();
    return status;
}

extern "C" __attribute__((visibility("default"))) uint32_t NdsAicpuRdmaRead(void *args) {
    auto *request = static_cast<nds_device_operation_request *>(args);
    auto *result = request == nullptr ? nullptr : reinterpret_cast<nds_device_operation_result *>(request->operation_result_address);
    if (!valid_operation_request(request)) { set_invalid(result); return kEntryInvalidArgument; }
    const uint32_t status = NdsAicpuRdmaReadImpl(&request->connection, &request->parameters.transfer, result);
    entry_barrier();
    return status;
}

extern "C" __attribute__((visibility("default"))) uint32_t NdsAicpuRdmaWrite(void *args) {
    auto *request = static_cast<nds_device_operation_request *>(args);
    auto *result = request == nullptr ? nullptr : reinterpret_cast<nds_device_operation_result *>(request->operation_result_address);
    if (!valid_operation_request(request)) { set_invalid(result); return kEntryInvalidArgument; }
    const uint32_t status = NdsAicpuRdmaWriteImpl(&request->connection, &request->parameters.transfer, result);
    entry_barrier();
    return status;
}

extern "C" __attribute__((visibility("default"))) uint32_t NdsAicpuStorageRead(void *args) {
    auto *request = static_cast<nds_device_storage_request *>(args);
    auto *result = request == nullptr ? nullptr : reinterpret_cast<nds_device_operation_result *>(request->operation_result_address);
    if (!valid_storage_request(request)) { set_invalid(result); return kEntryInvalidArgument; }
    const uint32_t status = NdsAicpuStorageReadImpl(&request->storage, &request->io, result);
    entry_barrier();
    return status;
}

extern "C" __attribute__((visibility("default"))) uint32_t NdsAicpuStorageWrite(void *args) {
    auto *request = static_cast<nds_device_storage_request *>(args);
    auto *result = request == nullptr ? nullptr : reinterpret_cast<nds_device_operation_result *>(request->operation_result_address);
    if (!valid_storage_request(request)) { set_invalid(result); return kEntryInvalidArgument; }
    const uint32_t status = NdsAicpuStorageWriteImpl(&request->storage, &request->io, result);
    entry_barrier();
    return status;
}
