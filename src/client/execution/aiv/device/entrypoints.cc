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

template <typename Args>
__aicore__ inline bool ValidVerbsArgs(__gm__ const Args *args) {
    return args != nullptr;
}

template <typename Args>
__aicore__ inline bool ValidRdmaArgs(__gm__ const Args *args) {
    return args != nullptr;
}

template <typename Args>
__aicore__ inline bool ValidStorageArgs(__gm__ const Args *args) {
    return args != nullptr;
}
}  // namespace

extern "C" __global__ __aicore__ void NdsAivPostSend(GM_ADDR args_address) {
    __gm__ auto *args = reinterpret_cast<__gm__ NdsDevicePostSendArgs *>(args_address);
    if (!ValidVerbsArgs(args))
        return SetInvalid(args == nullptr ? nullptr : &args->return_value);
    NdsDeviceSendWr send_wr = LoadSendWr(&args->wr);
    TPipe pipe;
    TBuf<> scratch;
    pipe.InitBuffer(scratch, 64U);
    NdsAivPostSendImpl(&args->qp, &send_wr, &args->return_value, &scratch);
}

/* Launches a single AIV submission for a contiguous device-global WR array. */
extern "C" __global__ __aicore__ void NdsAivPostSendBatch(GM_ADDR args_address) {
    __gm__ auto *args = reinterpret_cast<__gm__ NdsDevicePostSendBatchArgs *>(args_address);
    if (!ValidVerbsArgs(args))
        return SetInvalid(args == nullptr ? nullptr : &args->return_value);
    __gm__ const auto *wrs = reinterpret_cast<__gm__ const NdsDeviceSendWr *>(args->wrs_address);
    TPipe pipe;
    TBuf<> scratch;
    pipe.InitBuffer(scratch, 64U);
    NdsAivPostSendBatchImpl(&args->qp, wrs, args->wr_count, &args->return_value, &args->bad_wr_address, &scratch);
}

extern "C" __global__ __aicore__ void NdsAivPostRecv(GM_ADDR args_address) {
    __gm__ auto *args = reinterpret_cast<__gm__ NdsDevicePostRecvArgs *>(args_address);
    if (!ValidVerbsArgs(args))
        return SetInvalid(args == nullptr ? nullptr : &args->return_value);
    NdsDeviceRecvWr recv{};
    recv.wr_id = args->wr.wr_id;
    recv.local.address = args->wr.local.address;
    recv.local.length = args->wr.local.length;
    recv.local.local_key = args->wr.local.local_key;
    TPipe pipe;
    TBuf<> scratch;
    pipe.InitBuffer(scratch, 64U);
    NdsAivPostRecvImpl(&args->qp, &recv, &args->return_value, &scratch);
}
extern "C" __global__ __aicore__ void NdsAivPollCq(GM_ADDR args_address) {
    __gm__ auto *args = reinterpret_cast<__gm__ NdsDevicePollCqArgs *>(args_address);
    if (!ValidVerbsArgs(args))
        return SetInvalid(args == nullptr ? nullptr : &args->return_value);
    TPipe pipe;
    TBuf<> scratch;
    pipe.InitBuffer(scratch, 64U);
    NdsAivPollCqImpl(&args->qp, args->is_send_cq, args->max_completions,
                     reinterpret_cast<__gm__ NdsDeviceWc *>(args->wc_address), &args->return_value, &scratch);
}
extern "C" __global__ __aicore__ void NdsAivRdmaSend(GM_ADDR args_address) {
    __gm__ auto *args = reinterpret_cast<__gm__ NdsDeviceRdmaSendArgs *>(args_address);
    if (!ValidRdmaArgs(args))
        return SetInvalid(args == nullptr ? nullptr : &args->return_value);
    TPipe pipe;
    TBuf<> scratch;
    pipe.InitBuffer(scratch, 64U);
    NdsDeviceSendWr send_wr = LoadSendWr(&args->wr);
    NdsAivRdmaSendImpl(&args->transport, &send_wr, &args->return_value, &scratch);
}

extern "C" __global__ __aicore__ void NdsAivRdmaRecv(GM_ADDR args_address) {
    __gm__ auto *args = reinterpret_cast<__gm__ NdsDeviceRdmaRecvArgs *>(args_address);
    if (!ValidRdmaArgs(args))
        return SetInvalid(args == nullptr ? nullptr : &args->return_value);
    TPipe pipe;
    TBuf<> scratch;
    pipe.InitBuffer(scratch, 64U);
    NdsDeviceRecvWr recv{};
    recv.wr_id = args->wr.wr_id;
    recv.local.address = args->wr.local.address;
    recv.local.length = args->wr.local.length;
    recv.local.local_key = args->wr.local.local_key;
    NdsAivRdmaRecvImpl(&args->transport, &recv, &args->return_value, &scratch);
}

extern "C" __global__ __aicore__ void NdsAivRdmaRead(GM_ADDR args_address) {
    __gm__ auto *args = reinterpret_cast<__gm__ NdsDeviceRdmaReadArgs *>(args_address);
    if (!ValidRdmaArgs(args))
        return SetInvalid(args == nullptr ? nullptr : &args->return_value);
    TPipe pipe;
    TBuf<> scratch;
    pipe.InitBuffer(scratch, 64U);
    NdsDeviceSendWr send_wr = LoadSendWr(&args->wr);
    NdsAivRdmaReadImpl(&args->transport, &send_wr, &args->return_value, &scratch);
}

extern "C" __global__ __aicore__ void NdsAivRdmaWrite(GM_ADDR args_address) {
    __gm__ auto *args = reinterpret_cast<__gm__ NdsDeviceRdmaWriteArgs *>(args_address);
    if (!ValidRdmaArgs(args))
        return SetInvalid(args == nullptr ? nullptr : &args->return_value);
    TPipe pipe;
    TBuf<> scratch;
    pipe.InitBuffer(scratch, 64U);
    NdsDeviceSendWr send_wr = LoadSendWr(&args->wr);
    NdsAivRdmaWriteImpl(&args->transport, &send_wr, &args->return_value, &scratch);
}

extern "C" __global__ __aicore__ void NdsAivStorageRead(GM_ADDR args_address) {
    __gm__ auto *args = reinterpret_cast<__gm__ NdsDeviceStorageReadArgs *>(args_address);
    if (!ValidStorageArgs(args))
        return SetInvalid(args == nullptr ? nullptr : &args->return_value);
    TPipe pipe;
    TBuf<> scratch;
    pipe.InitBuffer(scratch, 64U);
    NdsAivStorageReadImpl(&args->context, &args->command, &args->return_value, &scratch);
}

extern "C" __global__ __aicore__ void NdsAivStorageWrite(GM_ADDR args_address) {
    __gm__ auto *args = reinterpret_cast<__gm__ NdsDeviceStorageWriteArgs *>(args_address);
    if (!ValidStorageArgs(args))
        return SetInvalid(args == nullptr ? nullptr : &args->return_value);
    TPipe pipe;
    TBuf<> scratch;
    pipe.InitBuffer(scratch, 64U);
    NdsAivStorageWriteImpl(&args->context, &args->command, &args->return_value, &scratch);
}

extern "C" __global__ __aicore__ void NdsAivStorageBatchRead(GM_ADDR args_address) {
    __gm__ auto *args = reinterpret_cast<__gm__ NdsDeviceStorageBatchReadArgs *>(args_address);
    if (!ValidStorageArgs(args))
        return SetInvalid(args == nullptr ? nullptr : &args->return_value);
    TPipe pipe;
    TBuf<> scratch;
    pipe.InitBuffer(scratch, 64U);
    NdsAivStorageBatchReadImpl(&args->context, &args->command, &args->return_value, &scratch);
}

extern "C" __global__ __aicore__ void NdsAivStorageBatchWrite(GM_ADDR args_address) {
    __gm__ auto *args = reinterpret_cast<__gm__ NdsDeviceStorageBatchWriteArgs *>(args_address);
    if (!ValidStorageArgs(args))
        return SetInvalid(args == nullptr ? nullptr : &args->return_value);
    TPipe pipe;
    TBuf<> scratch;
    pipe.InitBuffer(scratch, 64U);
    NdsAivStorageBatchWriteImpl(&args->context, &args->command, &args->return_value, &scratch);
}

extern "C" __global__ __aicore__ void NdsAivStorageWait(GM_ADDR args_address) {
    __gm__ auto *args = reinterpret_cast<__gm__ NdsDeviceStorageWaitArgs *>(args_address);
    if (!ValidStorageArgs(args))
        return SetInvalid(args == nullptr ? nullptr : &args->return_value);
    NdsAivStorageWaitImpl(&args->context, args->command_id, args->expected_bytes, &args->return_value);
}

static const struct FunLevelKType NdsAivPostSend_kernel_type_section
    __attribute__((used, section(".ascend.meta.NdsAivPostSend"))) = {{F_TYPE_KTYPE, sizeof(unsigned int), K_TYPE_AIV}};
static const struct FunLevelKType NdsAivPostSendBatch_kernel_type_section __attribute__((
    used, section(".ascend.meta.NdsAivPostSendBatch"))) = {{F_TYPE_KTYPE, sizeof(unsigned int), K_TYPE_AIV}};
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
static const struct FunLevelKType NdsAivStorageWait_kernel_type_section __attribute__((
    used, section(".ascend.meta.NdsAivStorageWait"))) = {{F_TYPE_KTYPE, sizeof(unsigned int), K_TYPE_AIV}};
