#ifndef NDS_AIV_DEVICE_INTERNAL_H
#define NDS_AIV_DEVICE_INTERNAL_H

#include "kernel_operator.h"
#include "backend_verbs.h"

using namespace AscendC;

__aicore__ inline void NdsAivCacheSync(__gm__ uint8_t *address, uint64_t length) {
    __gm__ uint8_t *start = (__gm__ uint8_t *)((uint64_t)address / CACHE_LINE_SIZE * CACHE_LINE_SIZE);
    __gm__ uint8_t *end = (__gm__ uint8_t *)(((uint64_t)address + length) / CACHE_LINE_SIZE * CACHE_LINE_SIZE);
    GlobalTensor<uint8_t> global;
    global.SetGlobalBuffer(start);
    for (uint32_t offset = 0U; offset <= end - start; offset += CACHE_LINE_SIZE)
        DataCacheCleanAndInvalid<uint8_t, CacheLine::SINGLE_CACHE_LINE, DcciDst::CACHELINE_OUT>(global[offset]);
}

__aicore__ inline void NdsAivSetReturnValue(__gm__ int32_t *return_value, uint32_t status) {
    if (return_value == nullptr)
        return;
    *return_value = status == NDS_OPERATION_SUCCESS ? 0 : -static_cast<int32_t>(status);
    NdsAivCacheSync(reinterpret_cast<__gm__ uint8_t *>(return_value), sizeof(*return_value));
}

__aicore__ inline bool NdsAivValidQp(__gm__ const NdsQpDescriptor *qp) {
    return qp != nullptr;
}

#endif
