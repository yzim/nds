#ifndef NDS_ACL_LOADER_H
#define NDS_ACL_LOADER_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Minimal AscendCL lifecycle adapter.  The default build resolves this public
 * ABI from the selected CANN installation at runtime.  A version-pinned build
 * may instead link the official ACL headers/library behind this same adapter
 * (NDS_LINK_PUBLIC_ACL), without changing the consumer-facing API.
 */
typedef int (*NdsAclInitFn)(const char *config_path);
typedef int (*NdsAclFinalizeFn)(void);
typedef int (*NdsAclRtSetDeviceFn)(int32_t device_id);
typedef int (*NdsAclRtGetPhyDevIdFn)(int32_t logic_device_id, int32_t *physical_device_id);
typedef int (*NdsAclRtMallocFn)(void **device_ptr, size_t size, int policy);
typedef int (*NdsAclRtFreeFn)(void *device_ptr);
typedef int (*NdsAclRtHostRegisterFn)(void *host_ptr, uint64_t size, int type, void **device_ptr);
typedef int (*NdsAclRtHostUnregisterFn)(void *host_ptr);
typedef int (*NdsAclRtMemcpyFn)(void *dst, size_t dst_max, const void *src, size_t count, int kind);
typedef int (*NdsAclRtMemsetFn)(void *device_ptr, size_t max_count, int32_t value, size_t count);
typedef void *NdsAclBinHandle;
typedef void *NdsAclFuncHandle;
typedef void *NdsAclStream;

typedef enum NdsAclBinaryLoadOptionType {
    NDS_ACL_BINARY_LOAD_OPT_LAZY_LOAD = 1,
    NDS_ACL_BINARY_LOAD_OPT_CPU_KERNEL_MODE = 3,
} NdsAclBinaryLoadOptionType;

typedef enum NdsAclCpuKernelMode {
    NDS_ACL_CPU_KERNEL_REGISTER_JSON = 0,
    NDS_ACL_CPU_KERNEL_LOAD_SO_AND_JSON = 1,
} NdsAclCpuKernelMode;

typedef union NdsAclBinaryLoadOptionValue {
    uint32_t lazy_load;
    int32_t cpu_kernel_mode;
    uint32_t reserved[4];
} NdsAclBinaryLoadOptionValue;

typedef struct NdsAclBinaryLoadOption {
    NdsAclBinaryLoadOptionType type;
    NdsAclBinaryLoadOptionValue value;
} NdsAclBinaryLoadOption;

typedef struct NdsAclBinaryLoadOptions {
    NdsAclBinaryLoadOption *options;
    size_t num_options;
} NdsAclBinaryLoadOptions;

typedef enum NdsAclLaunchKernelAttrId {
    NDS_ACL_LAUNCH_KERNEL_ATTR_SCHEM_MODE = 1,
    NDS_ACL_LAUNCH_KERNEL_ATTR_ENGINE_TYPE = 3,
    NDS_ACL_LAUNCH_KERNEL_ATTR_TIMEOUT = 7,
    NDS_ACL_LAUNCH_KERNEL_ATTR_TIMEOUT_US = 8,
} NdsAclLaunchKernelAttrId;

typedef union NdsAclLaunchKernelAttrValue {
    uint8_t schem_mode;
    int32_t engine_type;
    struct {
        uint32_t low;
        uint32_t high;
    } timeout_us;
    uint16_t timeout_seconds;
    uint32_t reserved[4];
} NdsAclLaunchKernelAttrValue;

typedef struct NdsAclLaunchKernelAttr {
    NdsAclLaunchKernelAttrId id;
    NdsAclLaunchKernelAttrValue value;
} NdsAclLaunchKernelAttr;

typedef struct NdsAclLaunchKernelConfig {
    NdsAclLaunchKernelAttr *attrs;
    size_t num_attrs;
} NdsAclLaunchKernelConfig;

typedef int (*NdsAclRtBinaryLoadFromFileFn)(const char *file_name, NdsAclBinaryLoadOptions *options,
                                            NdsAclBinHandle *bin_handle);
typedef int (*NdsAclRtBinaryUnloadFn)(NdsAclBinHandle bin_handle);
typedef int (*NdsAclRtBinaryGetFunctionFn)(NdsAclBinHandle bin_handle, const char *name,
                                           NdsAclFuncHandle *function_handle);
typedef int (*NdsAclRtLaunchKernelWithHostArgsFn)(NdsAclFuncHandle function_handle, uint32_t block_count,
                                                  NdsAclStream stream, NdsAclLaunchKernelConfig *config,
                                                  void *host_args, size_t args_size, void *placeholder_array,
                                                  size_t placeholder_count);
typedef int (*NdsAclRtCreateStreamFn)(NdsAclStream *stream);
typedef int (*NdsAclRtCreateStreamWithConfigFn)(NdsAclStream *stream, uint32_t priority, uint32_t flags);
typedef int (*NdsAclRtDestroyStreamFn)(NdsAclStream stream);
typedef int (*NdsAclRtSynchronizeStreamWithTimeoutFn)(NdsAclStream stream, int32_t timeout_ms);

enum {
    /* aclrtMemMallocPolicy values needed by the direct NPU data path. */
    NDS_ACL_MEM_MALLOC_HUGE_FIRST = 0,
    NDS_ACL_MEM_TYPE_HIGH_BANDWIDTH = 0x1000,
    /* HCOMM v9.0.0 hrtMalloc policy for non-310P NPUs. */
    NDS_ACL_MEM_MALLOC_DIRECT_NPU = NDS_ACL_MEM_MALLOC_HUGE_FIRST | NDS_ACL_MEM_TYPE_HIGH_BANDWIDTH,
    NDS_ACL_MEMCPY_HOST_TO_DEVICE = 1,
    NDS_ACL_MEMCPY_DEVICE_TO_HOST = 2,
    NDS_ACL_STREAM_FAST_LAUNCH = 0x1,
    NDS_ACL_STREAM_FAST_SYNC = 0x2,
    NDS_ACL_ENGINE_TYPE_AIV = 1,
    NDS_ACL_HOST_REGISTER_MAPPED = 0,
};

enum { NDS_ACL_ERROR_CAPACITY = 512 };

typedef struct NdsAclApi {
    void *library;
    NdsAclInitFn init;
    NdsAclFinalizeFn finalize;
    NdsAclRtSetDeviceFn set_device;
    NdsAclRtGetPhyDevIdFn get_phy_dev_id;
    NdsAclRtMallocFn malloc_device;
    NdsAclRtFreeFn free_device;
    /* Optional CANN page-locked host-memory API. */
    NdsAclRtHostRegisterFn host_register;
    NdsAclRtHostUnregisterFn host_unregister;
    NdsAclRtMemcpyFn memcpy;
    NdsAclRtMemsetFn memset_device;
    NdsAclRtBinaryLoadFromFileFn binary_load_from_file;
    NdsAclRtBinaryUnloadFn binary_unload;
    NdsAclRtBinaryGetFunctionFn binary_get_function;
    NdsAclRtLaunchKernelWithHostArgsFn launch_kernel_with_host_args;
    NdsAclRtCreateStreamFn create_stream;
    NdsAclRtCreateStreamWithConfigFn create_stream_with_config;
    NdsAclRtDestroyStreamFn destroy_stream;
    NdsAclRtSynchronizeStreamWithTimeoutFn synchronize_stream_with_timeout;
    char error[NDS_ACL_ERROR_CAPACITY];
} NdsAclApi;

int nds_acl_open(NdsAclApi *api, const char *library_path);
void nds_acl_close(NdsAclApi *api);
const char *nds_acl_error(const NdsAclApi *api);

#ifdef __cplusplus
}
#endif

#endif
