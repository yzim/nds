#define NDS_AIV_DEVICE_API_LINKAGE static
#define NDS_STORAGE_SERDE_INLINE __aicore__ inline

#include "kernel_operator.h"
#include "api.h"

#include "verbs.cc"
#include "connection.cc"
#include "storage.cc"

using namespace AscendC;

namespace {
__aicore__ inline void SetInvalid(__gm__ int32_t *return_value) {
    NdsAivSetReturnValue(return_value, NDS_DEVICE_OPERATION_INVALID_ARGUMENT);
}

template <typename Request>
__aicore__ inline bool ValidVerbsRequest(__gm__ const Request *request) {
    return request != nullptr;
}

template <typename Request>
__aicore__ inline bool ValidRdmaRequest(__gm__ const Request *request) {
    return request != nullptr;
}

__aicore__ inline NdsDeviceSendWr LocalSendWr(__gm__ const NdsDeviceSendWr *wr) {
    NdsDeviceSendWr local{};
    local.wr_id = wr->wr_id;
    local.opcode = wr->opcode;
    local.flags = wr->flags;
    local.local.address = wr->local.address;
    local.local.length = wr->local.length;
    local.local.local_key = wr->local.local_key;
    local.remote_address = wr->remote_address;
    local.remote_key = wr->remote_key;
    local.reserved = wr->reserved;
    return local;
}

template <typename Args>
__aicore__ inline bool ValidStorageArgs(__gm__ const Args *request) {
    return request != nullptr;
}
}  // namespace

extern "C" __global__ __aicore__ void NdsAivPostSend(GM_ADDR request_address) {
    __gm__ auto *request = reinterpret_cast<__gm__ NdsDevicePostSendArgs *>(request_address);
    if (!ValidVerbsRequest(request))
        return SetInvalid(request == nullptr ? nullptr : &request->return_value);
    NdsDeviceSendWr send_wr = LocalSendWr(&request->wr);
    TPipe pipe;
    TBuf<> scratch;
    pipe.InitBuffer(scratch, 64U);
    NdsAivPostSendImpl(&request->qp, &send_wr, &request->return_value, &scratch);
}
extern "C" __global__ __aicore__ void NdsAivPostRecv(GM_ADDR request_address) {
    __gm__ auto *request = reinterpret_cast<__gm__ NdsDevicePostRecvArgs *>(request_address);
    if (!ValidVerbsRequest(request))
        return SetInvalid(request == nullptr ? nullptr : &request->return_value);
    NdsDeviceRecvWr recv{};
    recv.wr_id = request->wr.wr_id;
    recv.local.address = request->wr.local.address;
    recv.local.length = request->wr.local.length;
    recv.local.local_key = request->wr.local.local_key;
    TPipe pipe;
    TBuf<> scratch;
    pipe.InitBuffer(scratch, 64U);
    NdsAivPostRecvImpl(&request->qp, &recv, &request->return_value, &scratch);
}
extern "C" __global__ __aicore__ void NdsAivPollCq(GM_ADDR request_address) {
    __gm__ auto *request = reinterpret_cast<__gm__ NdsDevicePollCqArgs *>(request_address);
    if (!ValidVerbsRequest(request))
        return SetInvalid(request == nullptr ? nullptr : &request->return_value);
    TPipe pipe;
    TBuf<> scratch;
    pipe.InitBuffer(scratch, 64U);
    NdsAivPollCqImpl(&request->qp, request->is_send_cq, request->max_completions,
                     reinterpret_cast<__gm__ NdsDeviceWc *>(request->wc_address), &request->return_value, &scratch);
}
extern "C" __global__ __aicore__ void NdsAivRdmaSend(GM_ADDR request_address) {
    __gm__ auto *request = reinterpret_cast<__gm__ NdsDeviceRdmaSendArgs *>(request_address);
    if (!ValidRdmaRequest(request))
        return SetInvalid(request == nullptr ? nullptr : &request->return_value);
    TPipe pipe;
    TBuf<> scratch;
    pipe.InitBuffer(scratch, 64U);
    NdsDeviceSendWr send_wr = LocalSendWr(&request->wr);
    NdsAivRdmaSendImpl(&request->transport, &send_wr, &request->return_value, &scratch);
}

extern "C" __global__ __aicore__ void NdsAivRdmaRecv(GM_ADDR request_address) {
    __gm__ auto *request = reinterpret_cast<__gm__ NdsDeviceRdmaRecvArgs *>(request_address);
    if (!ValidRdmaRequest(request))
        return SetInvalid(request == nullptr ? nullptr : &request->return_value);
    TPipe pipe;
    TBuf<> scratch;
    pipe.InitBuffer(scratch, 64U);
    NdsAivRdmaRecvImpl(&request->transport, &request->wr, &request->return_value, &scratch);
}

extern "C" __global__ __aicore__ void NdsAivRdmaRead(GM_ADDR request_address) {
    __gm__ auto *request = reinterpret_cast<__gm__ NdsDeviceRdmaReadArgs *>(request_address);
    if (!ValidRdmaRequest(request))
        return SetInvalid(request == nullptr ? nullptr : &request->return_value);
    TPipe pipe;
    TBuf<> scratch;
    pipe.InitBuffer(scratch, 64U);
    NdsDeviceSendWr send_wr = LocalSendWr(&request->wr);
    NdsAivRdmaReadImpl(&request->transport, &send_wr, &request->return_value, &scratch);
}

extern "C" __global__ __aicore__ void NdsAivRdmaWrite(GM_ADDR request_address) {
    __gm__ auto *request = reinterpret_cast<__gm__ NdsDeviceRdmaWriteArgs *>(request_address);
    if (!ValidRdmaRequest(request))
        return SetInvalid(request == nullptr ? nullptr : &request->return_value);
    TPipe pipe;
    TBuf<> scratch;
    pipe.InitBuffer(scratch, 64U);
    NdsDeviceSendWr send_wr = LocalSendWr(&request->wr);
    NdsAivRdmaWriteImpl(&request->transport, &send_wr, &request->return_value, &scratch);
}

extern "C" __global__ __aicore__ void NdsAivStorageRead(GM_ADDR request_address) {
    __gm__ auto *request = reinterpret_cast<__gm__ NdsDeviceStorageReadArgs *>(request_address);
    if (!ValidStorageArgs(request))
        return SetInvalid(request == nullptr ? nullptr : &request->return_value);
    TPipe pipe;
    TBuf<> scratch;
    pipe.InitBuffer(scratch, 64U);
    NdsAivStorageReadImpl(&request->context, &request->command, &request->return_value, &scratch);
}

extern "C" __global__ __aicore__ void NdsAivStorageWrite(GM_ADDR request_address) {
    __gm__ auto *request = reinterpret_cast<__gm__ NdsDeviceStorageWriteArgs *>(request_address);
    if (!ValidStorageArgs(request))
        return SetInvalid(request == nullptr ? nullptr : &request->return_value);
    TPipe pipe;
    TBuf<> scratch;
    pipe.InitBuffer(scratch, 64U);
    NdsAivStorageWriteImpl(&request->context, &request->command, &request->return_value, &scratch);
}

extern "C" __global__ __aicore__ void NdsAivStorageBatchRead(GM_ADDR request_address) {
    __gm__ auto *request = reinterpret_cast<__gm__ NdsDeviceStorageBatchReadArgs *>(request_address);
    if (!ValidStorageArgs(request))
        return SetInvalid(request == nullptr ? nullptr : &request->return_value);
    TPipe pipe;
    TBuf<> scratch;
    pipe.InitBuffer(scratch, 64U);
    NdsAivStorageBatchReadImpl(&request->context, &request->command, &request->return_value, &scratch);
}

extern "C" __global__ __aicore__ void NdsAivStorageBatchWrite(GM_ADDR request_address) {
    __gm__ auto *request = reinterpret_cast<__gm__ NdsDeviceStorageBatchWriteArgs *>(request_address);
    if (!ValidStorageArgs(request))
        return SetInvalid(request == nullptr ? nullptr : &request->return_value);
    TPipe pipe;
    TBuf<> scratch;
    pipe.InitBuffer(scratch, 64U);
    NdsAivStorageBatchWriteImpl(&request->context, &request->command, &request->return_value, &scratch);
}

extern "C" __global__ __aicore__ void NdsAivStorageWait(GM_ADDR request_address) {
    __gm__ auto *request = reinterpret_cast<__gm__ NdsDeviceStorageWaitArgs *>(request_address);
    if (!ValidStorageArgs(request))
        return SetInvalid(request == nullptr ? nullptr : &request->return_value);
    NdsAivStorageWaitImpl(&request->context, request->command_id, request->expected_bytes, &request->return_value);
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
