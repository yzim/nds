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
typedef int (*nds_acl_init_fn)(const char *config_path);
typedef int (*nds_acl_finalize_fn)(void);
typedef int (*nds_acl_rt_set_device_fn)(int32_t device_id);
typedef int (*nds_acl_rt_malloc_fn)(void **device_ptr, size_t size, int policy);
typedef int (*nds_acl_rt_free_fn)(void *device_ptr);
typedef int (*nds_acl_rt_memcpy_fn)(void *dst, size_t dst_max, const void *src, size_t count, int kind);
typedef int (*nds_acl_rt_memset_fn)(void *device_ptr, size_t max_count, int32_t value, size_t count);
typedef void *nds_acl_bin_handle;
typedef void *nds_acl_func_handle;
typedef void *nds_acl_args_handle;
typedef void *nds_acl_param_handle;
typedef void *nds_acl_stream;

typedef enum nds_acl_binary_load_option_type {
    NDS_ACL_BINARY_LOAD_OPT_LAZY_LOAD = 1,
    NDS_ACL_BINARY_LOAD_OPT_CPU_KERNEL_MODE = 3,
} nds_acl_binary_load_option_type;

typedef enum nds_acl_cpu_kernel_mode {
    NDS_ACL_CPU_KERNEL_REGISTER_JSON = 0,
    NDS_ACL_CPU_KERNEL_LOAD_SO_AND_JSON = 1,
} nds_acl_cpu_kernel_mode;

typedef union nds_acl_binary_load_option_value {
    uint32_t lazy_load;
    int32_t cpu_kernel_mode;
    uint32_t reserved[4];
} nds_acl_binary_load_option_value;

typedef struct nds_acl_binary_load_option {
    nds_acl_binary_load_option_type type;
    nds_acl_binary_load_option_value value;
} nds_acl_binary_load_option;

typedef struct nds_acl_binary_load_options {
    nds_acl_binary_load_option *options;
    size_t num_options;
} nds_acl_binary_load_options;

typedef enum nds_acl_launch_kernel_attr_id {
    NDS_ACL_LAUNCH_KERNEL_ATTR_SCHEM_MODE = 1,
    NDS_ACL_LAUNCH_KERNEL_ATTR_ENGINE_TYPE = 3,
    NDS_ACL_LAUNCH_KERNEL_ATTR_TIMEOUT = 7,
    NDS_ACL_LAUNCH_KERNEL_ATTR_TIMEOUT_US = 8,
} nds_acl_launch_kernel_attr_id;

typedef union nds_acl_launch_kernel_attr_value {
    uint8_t schem_mode;
    int32_t engine_type;
    struct {
        uint32_t low;
        uint32_t high;
    } timeout_us;
    uint16_t timeout_seconds;
    uint32_t reserved[4];
} nds_acl_launch_kernel_attr_value;

typedef struct nds_acl_launch_kernel_attr {
    nds_acl_launch_kernel_attr_id id;
    nds_acl_launch_kernel_attr_value value;
} nds_acl_launch_kernel_attr;

typedef struct nds_acl_launch_kernel_config {
    nds_acl_launch_kernel_attr *attrs;
    size_t num_attrs;
} nds_acl_launch_kernel_config;

typedef int (*nds_acl_rt_binary_load_from_file_fn)(const char *file_name,
                                                    nds_acl_binary_load_options *options,
                                                    nds_acl_bin_handle *bin_handle);
typedef int (*nds_acl_rt_binary_unload_fn)(nds_acl_bin_handle bin_handle);
typedef int (*nds_acl_rt_binary_get_function_fn)(nds_acl_bin_handle bin_handle, const char *name,
                                                  nds_acl_func_handle *function_handle);
typedef int (*nds_acl_rt_kernel_args_init_fn)(nds_acl_func_handle function_handle,
                                              nds_acl_args_handle *args_handle);
typedef int (*nds_acl_rt_kernel_args_append_fn)(nds_acl_args_handle args_handle, void *parameter,
                                                size_t parameter_size, nds_acl_param_handle *parameter_handle);
typedef int (*nds_acl_rt_kernel_args_finalize_fn)(nds_acl_args_handle args_handle);
typedef int (*nds_acl_rt_launch_kernel_with_config_fn)(nds_acl_func_handle function_handle,
                                                       uint32_t block_count, nds_acl_stream stream,
                                                       nds_acl_launch_kernel_config *config,
                                                       nds_acl_args_handle args_handle, void *reserved);
typedef int (*nds_acl_rt_launch_kernel_with_host_args_fn)(nds_acl_func_handle function_handle,
                                                          uint32_t block_count, nds_acl_stream stream,
                                                          nds_acl_launch_kernel_config *config,
                                                          void *host_args, size_t args_size,
                                                          void *placeholder_array, size_t placeholder_count);
typedef int (*nds_acl_rt_create_stream_fn)(nds_acl_stream *stream);
typedef int (*nds_acl_rt_create_stream_with_config_fn)(nds_acl_stream *stream, uint32_t priority, uint32_t flags);
typedef int (*nds_acl_rt_destroy_stream_fn)(nds_acl_stream stream);
typedef int (*nds_acl_rt_synchronize_stream_with_timeout_fn)(nds_acl_stream stream, int32_t timeout_ms);

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
};

enum { NDS_ACL_ERROR_CAPACITY = 512 };

typedef struct nds_acl_api {
    void *library;
    nds_acl_init_fn init;
    nds_acl_finalize_fn finalize;
    nds_acl_rt_set_device_fn set_device;
    nds_acl_rt_malloc_fn malloc_device;
    nds_acl_rt_free_fn free_device;
    nds_acl_rt_memcpy_fn memcpy;
    nds_acl_rt_memset_fn memset_device;
    nds_acl_rt_binary_load_from_file_fn binary_load_from_file;
    nds_acl_rt_binary_unload_fn binary_unload;
    nds_acl_rt_binary_get_function_fn binary_get_function;
    nds_acl_rt_kernel_args_init_fn kernel_args_init;
    nds_acl_rt_kernel_args_append_fn kernel_args_append;
    nds_acl_rt_kernel_args_finalize_fn kernel_args_finalize;
    nds_acl_rt_launch_kernel_with_config_fn launch_kernel_with_config;
    nds_acl_rt_launch_kernel_with_host_args_fn launch_kernel_with_host_args;
    nds_acl_rt_create_stream_fn create_stream;
    nds_acl_rt_create_stream_with_config_fn create_stream_with_config;
    nds_acl_rt_destroy_stream_fn destroy_stream;
    nds_acl_rt_synchronize_stream_with_timeout_fn synchronize_stream_with_timeout;
    char error[NDS_ACL_ERROR_CAPACITY];
} nds_acl_api;

int nds_acl_open(nds_acl_api *api, const char *library_path);
void nds_acl_close(nds_acl_api *api);
const char *nds_acl_error(const nds_acl_api *api);

#ifdef __cplusplus
}
#endif

#endif
