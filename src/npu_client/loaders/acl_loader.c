#include "nds/acl_loader.h"

#include <dlfcn.h>
#ifdef NDS_LINK_PUBLIC_ACL
#include <acl/acl_rt.h>
#endif
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#ifdef NDS_LINK_PUBLIC_ACL
/* Public AscendCL ABI wrappers keep nds_acl_api's stable int-returning surface. */
static const int nds_linked_acl_library_marker = 0;
static int nds_linked_acl_init(const char *config_path)
{
    return (int)aclInit(config_path);
}

static int nds_linked_acl_finalize(void)
{
    return (int)aclFinalize();
}

static int nds_linked_acl_set_device(int32_t device_id)
{
    return (int)aclrtSetDevice(device_id);
}

static int nds_linked_acl_malloc_device(void **device_ptr, size_t size, int policy)
{
    return (int)aclrtMalloc(device_ptr, size, (aclrtMemMallocPolicy)policy);
}

static int nds_linked_acl_free_device(void *device_ptr)
{
    return (int)aclrtFree(device_ptr);
}

static int nds_linked_acl_memcpy(void *dst, size_t dst_max, const void *src, size_t count, int kind)
{
    return (int)aclrtMemcpy(dst, dst_max, src, count, (aclrtMemcpyKind)kind);
}

static int nds_linked_acl_memset_device(void *device_ptr, size_t max_count, int32_t value, size_t count)
{
    return (int)aclrtMemset(device_ptr, max_count, value, count);
}

static int nds_linked_acl_binary_load_from_file(const char *file_name, nds_acl_binary_load_options *options,
                                                nds_acl_bin_handle *bin_handle)
{
    return (int)aclrtBinaryLoadFromFile(file_name, (aclrtBinaryLoadOptions *)options, (aclrtBinHandle *)bin_handle);
}
static int nds_linked_acl_binary_unload(nds_acl_bin_handle bin_handle)
{
    return (int)aclrtBinaryUnLoad((aclrtBinHandle)bin_handle);
}
static int nds_linked_acl_binary_get_function(nds_acl_bin_handle bin_handle, const char *name,
                                              nds_acl_func_handle *function_handle)
{
    return (int)aclrtBinaryGetFunction((aclrtBinHandle)bin_handle, name, (aclrtFuncHandle *)function_handle);
}
static int nds_linked_acl_kernel_args_init(nds_acl_func_handle function_handle, nds_acl_args_handle *args_handle)
{
    return (int)aclrtKernelArgsInit((aclrtFuncHandle)function_handle, (aclrtArgsHandle *)args_handle);
}
static int nds_linked_acl_kernel_args_append(nds_acl_args_handle args_handle, void *parameter, size_t parameter_size,
                                             nds_acl_param_handle *parameter_handle)
{
    return (int)aclrtKernelArgsAppend((aclrtArgsHandle)args_handle, parameter, parameter_size,
                                      (aclrtParamHandle *)parameter_handle);
}
static int nds_linked_acl_kernel_args_finalize(nds_acl_args_handle args_handle)
{
    return (int)aclrtKernelArgsFinalize((aclrtArgsHandle)args_handle);
}
static int nds_linked_acl_launch_kernel_with_config(nds_acl_func_handle function_handle, uint32_t block_count,
                                                     nds_acl_stream stream, nds_acl_launch_kernel_config *config,
                                                     nds_acl_args_handle args_handle, void *reserved)
{
    return (int)aclrtLaunchKernelWithConfig((aclrtFuncHandle)function_handle, block_count, (aclrtStream)stream,
                                            (aclrtLaunchKernelCfg *)config, (aclrtArgsHandle)args_handle, reserved);
}
static int nds_linked_acl_create_stream(nds_acl_stream *stream)
{
    return (int)aclrtCreateStream((aclrtStream *)stream);
}
static int nds_linked_acl_create_stream_with_config(nds_acl_stream *stream, uint32_t priority, uint32_t flags)
{
    return (int)aclrtCreateStreamWithConfig((aclrtStream *)stream, priority, flags);
}
static int nds_linked_acl_destroy_stream(nds_acl_stream stream)
{
    return (int)aclrtDestroyStream((aclrtStream)stream);
}
static int nds_linked_acl_synchronize_stream_with_timeout(nds_acl_stream stream, int32_t timeout_ms)
{
    return (int)aclrtSynchronizeStreamWithTimeout((aclrtStream)stream, timeout_ms);
}
#endif

#ifndef NDS_LINK_PUBLIC_ACL
static void nds_acl_set_error(nds_acl_api *api, const char *format, ...)
{
    va_list arguments;

    va_start(arguments, format);
    (void)vsnprintf(api->error, sizeof(api->error), format, arguments);
    va_end(arguments);
}

static int nds_acl_resolve(nds_acl_api *api, const char *name, void *slot, size_t slot_size)
{
    const char *loader_error;
    void *symbol;

    if (slot_size != sizeof(symbol)) {
        nds_acl_set_error(api, "function-pointer size for %s differs from dlsym result size", name);
        return -1;
    }
    (void)dlerror();
    symbol = dlsym(api->library, name);
    loader_error = dlerror();
    if (loader_error != NULL || symbol == NULL) {
        nds_acl_set_error(api, "required symbol %s is unavailable: %s", name,
                          loader_error == NULL ? "returned null" : loader_error);
        return -1;
    }
    memcpy(slot, &symbol, slot_size);
    return 0;
}
#endif

int nds_acl_open(nds_acl_api *api, const char *library_path)
{
    if (api == NULL) {
        return -1;
    }

#ifdef NDS_LINK_PUBLIC_ACL
    (void)library_path;
    nds_acl_close(api);
    api->library = (void *)&nds_linked_acl_library_marker;
    api->init = nds_linked_acl_init;
    api->finalize = nds_linked_acl_finalize;
    api->set_device = nds_linked_acl_set_device;
    api->malloc_device = nds_linked_acl_malloc_device;
    api->free_device = nds_linked_acl_free_device;
    api->memcpy = nds_linked_acl_memcpy;
    api->memset_device = nds_linked_acl_memset_device;
    api->binary_load_from_file = nds_linked_acl_binary_load_from_file;
    api->binary_unload = nds_linked_acl_binary_unload;
    api->binary_get_function = nds_linked_acl_binary_get_function;
    api->kernel_args_init = nds_linked_acl_kernel_args_init;
    api->kernel_args_append = nds_linked_acl_kernel_args_append;
    api->kernel_args_finalize = nds_linked_acl_kernel_args_finalize;
    api->launch_kernel_with_config = nds_linked_acl_launch_kernel_with_config;
    api->create_stream = nds_linked_acl_create_stream;
    api->create_stream_with_config = nds_linked_acl_create_stream_with_config;
    api->destroy_stream = nds_linked_acl_destroy_stream;
    api->synchronize_stream_with_timeout = nds_linked_acl_synchronize_stream_with_timeout;
    api->error[0] = '\0';
    return 0;
#else
    const char *loader_error;

    if (library_path == NULL || library_path[0] == '\0') {
        nds_acl_set_error(api, "a non-empty AscendCL library path is required");
        return -1;
    }

    nds_acl_close(api);
    api->library = dlopen(library_path, RTLD_NOW | RTLD_LOCAL);
    if (api->library == NULL) {
        loader_error = dlerror();
        nds_acl_set_error(api, "unable to load %s: %s", library_path,
                          loader_error == NULL ? "unknown dynamic-loader error" : loader_error);
        return -1;
    }

#define NDS_ACL_RESOLVE(field, symbol) \
    do { \
        if (nds_acl_resolve(api, symbol, &api->field, sizeof(api->field)) != 0) { \
            nds_acl_close(api); \
            return -1; \
        } \
    } while (0)

    NDS_ACL_RESOLVE(init, "aclInit");
    NDS_ACL_RESOLVE(finalize, "aclFinalize");
    NDS_ACL_RESOLVE(set_device, "aclrtSetDevice");
    NDS_ACL_RESOLVE(malloc_device, "aclrtMalloc");
    NDS_ACL_RESOLVE(free_device, "aclrtFree");
    NDS_ACL_RESOLVE(memcpy, "aclrtMemcpy");
    NDS_ACL_RESOLVE(memset_device, "aclrtMemset");
    NDS_ACL_RESOLVE(binary_load_from_file, "aclrtBinaryLoadFromFile");
    NDS_ACL_RESOLVE(binary_unload, "aclrtBinaryUnLoad");
    NDS_ACL_RESOLVE(binary_get_function, "aclrtBinaryGetFunction");
    NDS_ACL_RESOLVE(kernel_args_init, "aclrtKernelArgsInit");
    NDS_ACL_RESOLVE(kernel_args_append, "aclrtKernelArgsAppend");
    NDS_ACL_RESOLVE(kernel_args_finalize, "aclrtKernelArgsFinalize");
    NDS_ACL_RESOLVE(launch_kernel_with_config, "aclrtLaunchKernelWithConfig");
    NDS_ACL_RESOLVE(create_stream, "aclrtCreateStream");
    NDS_ACL_RESOLVE(create_stream_with_config, "aclrtCreateStreamWithConfig");
    NDS_ACL_RESOLVE(destroy_stream, "aclrtDestroyStream");
    NDS_ACL_RESOLVE(synchronize_stream_with_timeout, "aclrtSynchronizeStreamWithTimeout");

#undef NDS_ACL_RESOLVE
    api->error[0] = '\0';
    return 0;
#endif
}

void nds_acl_close(nds_acl_api *api)
{
    if (api == NULL) {
        return;
    }
#ifndef NDS_LINK_PUBLIC_ACL
    if (api->library != NULL) {
        (void)dlclose(api->library);
    }
#endif
    memset(api, 0, sizeof(*api));
}

const char *nds_acl_error(const nds_acl_api *api)
{
    return api == NULL ? "no loader state" : api->error;
}
