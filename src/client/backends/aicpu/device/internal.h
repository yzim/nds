#ifndef NDS_AICPU_DEVICE_INTERNAL_H
#define NDS_AICPU_DEVICE_INTERNAL_H

#include "device_verbs.h"

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

inline bool NdsAicpuValidQp(const NdsQpDescriptor *qp) {
    return qp != nullptr;
}

inline void NdsAicpuSetReturnValue(int32_t *return_value, uint32_t status) {
    if (return_value != nullptr)
        *return_value = status == NDS_OPERATION_SUCCESS ? 0 : -static_cast<int32_t>(status);
    NdsAicpuBarrier();
}

#endif
