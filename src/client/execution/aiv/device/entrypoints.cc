#define NDS_AIV_DEVICE_API_LINKAGE static
#define NDS_STORAGE_SERDE_INLINE __aicore__ inline

#include "kernel_operator.h"
#include "api.h"

#include "nds/device_operator_args.h"

#include "verbs.cc"
#include "connection.cc"
#include "storage.cc"

using namespace AscendC;

namespace {
__aicore__ inline void SetInvalid(__gm__ NdsDeviceOperationResult *result) {
    if (result == nullptr)
        return;
    result->status = NDS_DEVICE_OPERATION_INVALID_ARGUMENT;
    result->path = NDS_DEVICE_OPERATION_PATH_NONE;
    result->provider_result = 0;
    result->reserved = 0U;
}

template <typename Request>
__aicore__ inline bool ValidRequest(__gm__ const Request *request, uint64_t result_address) {
    return request != nullptr && request->abi_version == NDS_DEVICE_OPERATOR_ARGS_ABI_VERSION &&
           request->size == sizeof(*request) && result_address != 0U;
}

__aicore__ inline bool ValidOperationRequest(__gm__ const NdsDeviceOperationRequest *request) {
    return request != nullptr && request->abi_version == NDS_DEVICE_OPERATIONS_ABI_VERSION &&
           request->size == sizeof(*request) && request->operation_result_address != 0U &&
           request->transport.abi_version == NDS_DEVICE_TRANSPORT_ABI_VERSION;
}

template <typename Args>
__aicore__ inline bool ValidStorageArgs(__gm__ const Args *request) {
    return request != nullptr && request->abi_version == NDS_DEVICE_STORAGE_ABI_VERSION &&
           request->size == sizeof(*request) && request->operation_result_address != 0U;
}
}  // namespace

extern "C" __global__ __aicore__ void NdsAivPostSend(GM_ADDR request_address) {
    __gm__ auto *request = reinterpret_cast<__gm__ NdsDevicePostSendRequest *>(request_address);
    __gm__ auto *result =
        request == nullptr ? nullptr
                           : reinterpret_cast<__gm__ NdsDeviceOperationResult *>(request->operation_result_address);
    if (!ValidRequest(request, request == nullptr ? 0U : request->operation_result_address))
        return SetInvalid(result);
    NdsDeviceSendWr wr{};
    wr.wr_id = request->wr.wr_id;
    wr.opcode = request->wr.opcode;
    wr.flags = request->wr.flags;
    wr.local.address = request->wr.local.address;
    wr.local.length = request->wr.local.length;
    wr.local.local_key = request->wr.local.local_key;
    wr.remote_address = request->wr.remote_address;
    wr.remote_key = request->wr.remote_key;
    wr.reserved = request->wr.reserved;
    TPipe pipe;
    TBuf<> scratch;
    pipe.InitBuffer(scratch, 64U);
    NdsAivPostSendImpl(&request->qp, &wr, &scratch, result);
}
extern "C" __global__ __aicore__ void NdsAivPostRecv(GM_ADDR request_address) {
    __gm__ auto *request = reinterpret_cast<__gm__ NdsDevicePostRecvRequest *>(request_address);
    __gm__ auto *result =
        request == nullptr ? nullptr
                           : reinterpret_cast<__gm__ NdsDeviceOperationResult *>(request->operation_result_address);
    if (!ValidRequest(request, request == nullptr ? 0U : request->operation_result_address))
        return SetInvalid(result);
    NdsDeviceRecvWr wr{};
    wr.wr_id = request->wr.wr_id;
    wr.local.address = request->wr.local.address;
    wr.local.length = request->wr.local.length;
    wr.local.local_key = request->wr.local.local_key;
    TPipe pipe;
    TBuf<> scratch;
    pipe.InitBuffer(scratch, 64U);
    NdsAivPostRecvImpl(&request->qp, &wr, &scratch, result);
}
extern "C" __global__ __aicore__ void NdsAivPollCq(GM_ADDR request_address) {
    __gm__ auto *request = reinterpret_cast<__gm__ NdsDeviceOperationRequest *>(request_address);
    __gm__ auto *result =
        request == nullptr ? nullptr
                           : reinterpret_cast<__gm__ NdsDeviceOperationResult *>(request->operation_result_address);
    if (!ValidOperationRequest(request))
        return SetInvalid(result);
    TPipe pipe;
    TBuf<> scratch;
    pipe.InitBuffer(scratch, 64U);
    NdsAivPollCqImpl(&request->transport.control_qp, &request->parameters.poll_cq, &scratch, result);
}
extern "C" __global__ __aicore__ void NdsAivRdmaSend(GM_ADDR request_address) {
    __gm__ auto *request = reinterpret_cast<__gm__ NdsDeviceOperationRequest *>(request_address);
    __gm__ auto *result =
        request == nullptr ? nullptr
                           : reinterpret_cast<__gm__ NdsDeviceOperationResult *>(request->operation_result_address);
    if (!ValidOperationRequest(request))
        return SetInvalid(result);
    TPipe pipe;
    TBuf<> scratch;
    pipe.InitBuffer(scratch, 64U);
    NdsAivRdmaSendImpl(&request->transport, &request->parameters.send_wr, &scratch, result);
}

extern "C" __global__ __aicore__ void NdsAivRdmaRecv(GM_ADDR request_address) {
    __gm__ auto *request = reinterpret_cast<__gm__ NdsDeviceOperationRequest *>(request_address);
    __gm__ auto *result =
        request == nullptr ? nullptr
                           : reinterpret_cast<__gm__ NdsDeviceOperationResult *>(request->operation_result_address);
    if (!ValidOperationRequest(request))
        return SetInvalid(result);
    TPipe pipe;
    TBuf<> scratch;
    pipe.InitBuffer(scratch, 64U);
    NdsAivRdmaRecvImpl(&request->transport, &request->parameters.send_wr, &scratch, result);
}

extern "C" __global__ __aicore__ void NdsAivRdmaRead(GM_ADDR request_address) {
    __gm__ auto *request = reinterpret_cast<__gm__ NdsDeviceOperationRequest *>(request_address);
    __gm__ auto *result =
        request == nullptr ? nullptr
                           : reinterpret_cast<__gm__ NdsDeviceOperationResult *>(request->operation_result_address);
    if (!ValidOperationRequest(request))
        return SetInvalid(result);
    TPipe pipe;
    TBuf<> scratch;
    pipe.InitBuffer(scratch, 64U);
    NdsAivRdmaReadImpl(&request->transport, &request->parameters.send_wr, &scratch, result);
}

extern "C" __global__ __aicore__ void NdsAivRdmaWrite(GM_ADDR request_address) {
    __gm__ auto *request = reinterpret_cast<__gm__ NdsDeviceOperationRequest *>(request_address);
    __gm__ auto *result =
        request == nullptr ? nullptr
                           : reinterpret_cast<__gm__ NdsDeviceOperationResult *>(request->operation_result_address);
    if (!ValidOperationRequest(request))
        return SetInvalid(result);
    TPipe pipe;
    TBuf<> scratch;
    pipe.InitBuffer(scratch, 64U);
    NdsAivRdmaWriteImpl(&request->transport, &request->parameters.send_wr, &scratch, result);
}

extern "C" __global__ __aicore__ void NdsAivStorageRead(GM_ADDR request_address) {
    __gm__ auto *request = reinterpret_cast<__gm__ NdsDeviceStorageReadArgs *>(request_address);
    __gm__ auto *result =
        request == nullptr ? nullptr
                           : reinterpret_cast<__gm__ NdsDeviceOperationResult *>(request->operation_result_address);
    if (!ValidStorageArgs(request))
        return SetInvalid(result);
    TPipe pipe;
    TBuf<> scratch;
    pipe.InitBuffer(scratch, 64U);
    NdsAivStorageReadImpl(&request->context, &request->command, &scratch, result);
}

extern "C" __global__ __aicore__ void NdsAivStorageWrite(GM_ADDR request_address) {
    __gm__ auto *request = reinterpret_cast<__gm__ NdsDeviceStorageWriteArgs *>(request_address);
    __gm__ auto *result =
        request == nullptr ? nullptr
                           : reinterpret_cast<__gm__ NdsDeviceOperationResult *>(request->operation_result_address);
    if (!ValidStorageArgs(request))
        return SetInvalid(result);
    TPipe pipe;
    TBuf<> scratch;
    pipe.InitBuffer(scratch, 64U);
    NdsAivStorageWriteImpl(&request->context, &request->command, &scratch, result);
}

extern "C" __global__ __aicore__ void NdsAivStorageBatchRead(GM_ADDR request_address) {
    __gm__ auto *request = reinterpret_cast<__gm__ NdsDeviceStorageBatchReadArgs *>(request_address);
    __gm__ auto *result =
        request == nullptr ? nullptr
                           : reinterpret_cast<__gm__ NdsDeviceOperationResult *>(request->operation_result_address);
    if (!ValidStorageArgs(request))
        return SetInvalid(result);
    TPipe pipe;
    TBuf<> scratch;
    pipe.InitBuffer(scratch, 64U);
    NdsAivStorageBatchReadImpl(&request->context, &request->command, &scratch, result);
}

extern "C" __global__ __aicore__ void NdsAivStorageBatchWrite(GM_ADDR request_address) {
    __gm__ auto *request = reinterpret_cast<__gm__ NdsDeviceStorageBatchWriteArgs *>(request_address);
    __gm__ auto *result =
        request == nullptr ? nullptr
                           : reinterpret_cast<__gm__ NdsDeviceOperationResult *>(request->operation_result_address);
    if (!ValidStorageArgs(request))
        return SetInvalid(result);
    TPipe pipe;
    TBuf<> scratch;
    pipe.InitBuffer(scratch, 64U);
    NdsAivStorageBatchWriteImpl(&request->context, &request->command, &scratch, result);
}

extern "C" __global__ __aicore__ void NdsAivStorageWait(GM_ADDR request_address) {
    __gm__ auto *request = reinterpret_cast<__gm__ NdsDeviceStorageWaitArgs *>(request_address);
    __gm__ auto *result =
        request == nullptr ? nullptr
                           : reinterpret_cast<__gm__ NdsDeviceOperationResult *>(request->operation_result_address);
    if (!ValidStorageArgs(request))
        return SetInvalid(result);
    NdsAivStorageWaitImpl(&request->context, request->command_id, request->expected_bytes, result);
}

static const struct FunLevelKType NdsAivPostSend_kernel_type_section
    __attribute__((used, section(".ascend.meta.NdsAivPostSend"))) = {{F_TYPE_KTYPE, sizeof(unsigned int), K_TYPE_AIV}};
static const struct FunLevelKType NdsAivPostRecv_kernel_type_section
    __attribute__((used, section(".ascend.meta.NdsAivPostRecv"))) = {{F_TYPE_KTYPE, sizeof(unsigned int), K_TYPE_AIV}};
static const struct FunLevelKType NdsAivPollCq_kernel_type_section
    __attribute__((used, section(".ascend.meta.NdsAivPollCq"))) = {{F_TYPE_KTYPE, sizeof(unsigned int), K_TYPE_AIV}};
static const struct FunLevelKType NdsAivRdmaSend_kernel_type_section
    __attribute__((used, section(".ascend.meta.NdsAivRdmaSend"))) = {{F_TYPE_KTYPE, sizeof(unsigned int), K_TYPE_AIV}};
static const struct FunLevelKType NdsAivRdmaRecv_kernel_type_section
    __attribute__((used, section(".ascend.meta.NdsAivRdmaRecv"))) = {{F_TYPE_KTYPE, sizeof(unsigned int), K_TYPE_AIV}};
static const struct FunLevelKType NdsAivRdmaRead_kernel_type_section
    __attribute__((used, section(".ascend.meta.NdsAivRdmaRead"))) = {{F_TYPE_KTYPE, sizeof(unsigned int), K_TYPE_AIV}};
static const struct FunLevelKType NdsAivRdmaWrite_kernel_type_section
    __attribute__((used, section(".ascend.meta.NdsAivRdmaWrite"))) = {{F_TYPE_KTYPE, sizeof(unsigned int), K_TYPE_AIV}};
static const struct FunLevelKType NdsAivStorageRead_kernel_type_section __attribute__((
    used, section(".ascend.meta.NdsAivStorageRead"))) = {{F_TYPE_KTYPE, sizeof(unsigned int), K_TYPE_AIV}};
static const struct FunLevelKType NdsAivStorageWrite_kernel_type_section __attribute__((
    used, section(".ascend.meta.NdsAivStorageWrite"))) = {{F_TYPE_KTYPE, sizeof(unsigned int), K_TYPE_AIV}};
static const struct FunLevelKType NdsAivStorageBatchRead_kernel_type_section __attribute__((
    used, section(".ascend.meta.NdsAivStorageBatchRead"))) = {{F_TYPE_KTYPE, sizeof(unsigned int), K_TYPE_AIV}};
static const struct FunLevelKType NdsAivStorageBatchWrite_kernel_type_section __attribute__((
    used, section(".ascend.meta.NdsAivStorageBatchWrite"))) = {{F_TYPE_KTYPE, sizeof(unsigned int), K_TYPE_AIV}};
static const struct FunLevelKType NdsAivStorageWait_kernel_type_section
    __attribute__((used, section(".ascend.meta.NdsAivStorageWait"))) = {{F_TYPE_KTYPE, sizeof(unsigned int), K_TYPE_AIV}};
