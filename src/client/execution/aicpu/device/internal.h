#ifndef NDS_AICPU_DEVICE_INTERNAL_H
#define NDS_AICPU_DEVICE_INTERNAL_H

#include "nds/device_qp.h"
#include "nds/device_verbs.h"

#include <dlfcn.h>
#include <stdint.h>

constexpr uint32_t kNdsAicpuSuccess = 0U;
constexpr uint32_t kNdsAicpuInvalidArgument = 1U;

inline void NdsAicpuBarrier() {
#if defined(__aarch64__)
    asm volatile("dsb st" ::: "memory");
#else
    asm volatile("" ::: "memory");
#endif
}

inline void *NdsAicpuResolveSymbol(const char *name) {
    constexpr char kHnsProviderLibrary[] = "libhns-rdmav25.so";
    constexpr char kVerbsLibrary[] = "libibverbs.so.1";
    static void *provider = dlopen(kHnsProviderLibrary, RTLD_NOW | RTLD_LOCAL);
    if (provider != nullptr) {
        void *symbol = dlsym(provider, name);
        if (symbol != nullptr)
            return symbol;
    }
    static void *verbs = dlopen(kVerbsLibrary, RTLD_NOW | RTLD_LOCAL);
    return verbs == nullptr ? nullptr : dlsym(verbs, name);
}

inline bool NdsAicpuValidQp(const nds_device_qp *qp) {
    return qp != nullptr && qp->abi_version == NDS_DEVICE_QP_ABI_VERSION && qp->size == sizeof(*qp);
}

inline void NdsAicpuSetResult(nds_device_operation_result *result, uint32_t status, uint32_t path,
                              int32_t provider_result) {
    result->status = status;
    result->path = path;
    result->provider_result = provider_result;
    result->reserved = 0U;
    NdsAicpuBarrier();
}

#endif
