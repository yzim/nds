#ifndef NDS_AIV_DEVICE_INTERNAL_H
#define NDS_AIV_DEVICE_INTERNAL_H

#include "kernel_operator.h"
#include "nds/device_verbs.h"

using namespace AscendC;

__aicore__ inline void NdsAivCacheSync(__gm__ uint8_t *address, uint64_t length) {
    __gm__ uint8_t *start = (__gm__ uint8_t *)((uint64_t)address / CACHE_LINE_SIZE * CACHE_LINE_SIZE);
    __gm__ uint8_t *end = (__gm__ uint8_t *)(((uint64_t)address + length) / CACHE_LINE_SIZE * CACHE_LINE_SIZE);
    GlobalTensor<uint8_t> global;
    global.SetGlobalBuffer(start);
    for (uint32_t offset = 0U; offset <= end - start; offset += CACHE_LINE_SIZE)
        DataCacheCleanAndInvalid<uint8_t, CacheLine::SINGLE_CACHE_LINE, DcciDst::CACHELINE_OUT>(global[offset]);
}

__aicore__ inline void NdsAivSetResult(__gm__ nds_device_operation_result *result, uint32_t status) {
    if (result == nullptr) return;
    result->status = status;
    result->path = NDS_DEVICE_OPERATION_PATH_DIRECT;
    result->provider_result = 0;
    result->reserved = 0U;
    NdsAivCacheSync(reinterpret_cast<__gm__ uint8_t *>(result), sizeof(*result));
}

__aicore__ inline bool NdsAivValidQp(__gm__ const nds_device_qp *qp) {
    return qp != nullptr && qp->abi_version == NDS_DEVICE_QP_ABI_VERSION && qp->size == sizeof(*qp);
}

#endif
