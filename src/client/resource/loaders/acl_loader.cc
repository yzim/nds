#include "nds/acl_loader.h"

#include <dlfcn.h>
#ifdef NDS_LINK_PUBLIC_ACL
#include <acl/acl_rt.h>
#endif
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#ifdef NDS_LINK_PUBLIC_ACL
/* Public AscendCL ABI wrappers keep NdsAclApi's stable int-returning surface. */
static const int nds_linked_acl_library_marker = 0;
static int nds_linked_acl_init(const char *config_path) {
    return (int)aclInit(config_path);
}

static int nds_linked_acl_finalize(void) {
    return (int)aclFinalize();
}

static int nds_linked_acl_set_device(int32_t device_id) {
    return (int)aclrtSetDevice(device_id);
}

static int nds_linked_acl_get_phy_dev_id(int32_t logic_device_id, int32_t *physical_device_id) {
    return (int)aclrtGetPhyDevIdByLogicDevId(logic_device_id, physical_device_id);
}

static int nds_linked_acl_malloc_device(void **device_ptr, size_t size, int policy) {
    return (int)aclrtMalloc(device_ptr, size, (aclrtMemMallocPolicy)policy);
}

static int nds_linked_acl_free_device(void *device_ptr) {
    return (int)aclrtFree(device_ptr);
}

static int nds_linked_acl_host_register(void *host_ptr, uint64_t size, int type, void **device_ptr) {
    return (int)aclrtHostRegister(host_ptr, size, (aclrtHostRegisterType)type, device_ptr);
}

static int nds_linked_acl_host_unregister(void *host_ptr) {
    return (int)aclrtHostUnregister(host_ptr);
}

static int nds_linked_acl_memcpy(void *dst, size_t dst_max, const void *src, size_t count, int kind) {
    return (int)aclrtMemcpy(dst, dst_max, src, count, (aclrtMemcpyKind)kind);
}

static int nds_linked_acl_memset_device(void *device_ptr, size_t max_count, int32_t value, size_t count) {
    return (int)aclrtMemset(device_ptr, max_count, value, count);
}

static int nds_linked_acl_binary_load_from_file(const char *file_name, NdsAclBinaryLoadOptions *options,
                                                NdsAclBinHandle *bin_handle) {
    return (int)aclrtBinaryLoadFromFile(file_name, (aclrtBinaryLoadOptions *)options, (aclrtBinHandle *)bin_handle);
}
static int nds_linked_acl_binary_unload(NdsAclBinHandle bin_handle) {
    return (int)aclrtBinaryUnLoad((aclrtBinHandle)bin_handle);
}
static int nds_linked_acl_binary_get_function(NdsAclBinHandle bin_handle, const char *name,
                                              NdsAclFuncHandle *function_handle) {
    return (int)aclrtBinaryGetFunction((aclrtBinHandle)bin_handle, name, (aclrtFuncHandle *)function_handle);
}
static int nds_linked_acl_launch_kernel_with_host_args(NdsAclFuncHandle function_handle, uint32_t block_count,
                                                       NdsAclStream stream, NdsAclLaunchKernelConfig *config,
                                                       void *host_args, size_t args_size, void *placeholder_array,
                                                       size_t placeholder_count) {
    return (int)aclrtLaunchKernelWithHostArgs((aclrtFuncHandle)function_handle, block_count, (aclrtStream)stream,
                                              (aclrtLaunchKernelCfg *)config, host_args, args_size,
                                              (aclrtPlaceHolderInfo *)placeholder_array, placeholder_count);
}
static int nds_linked_acl_create_stream(NdsAclStream *stream) {
    return (int)aclrtCreateStream((aclrtStream *)stream);
}
static int nds_linked_acl_create_stream_with_config(NdsAclStream *stream, uint32_t priority, uint32_t flags) {
    return (int)aclrtCreateStreamWithConfig((aclrtStream *)stream, priority, flags);
}
static int nds_linked_acl_destroy_stream(NdsAclStream stream) {
    return (int)aclrtDestroyStream((aclrtStream)stream);
}
static int nds_linked_acl_synchronize_stream_with_timeout(NdsAclStream stream, int32_t timeout_ms) {
    return (int)aclrtSynchronizeStreamWithTimeout((aclrtStream)stream, timeout_ms);
}
#endif

#ifndef NDS_LINK_PUBLIC_ACL
static void nds_acl_set_error(NdsAclApi *api, const char *format, ...) {
    va_list arguments;

    va_start(arguments, format);
    (void)vsnprintf(api->error, sizeof(api->error), format, arguments);
    va_end(arguments);
}

static int nds_acl_resolve(NdsAclApi *api, const char *name, void *slot, size_t slot_size) {
    const char *loader_error;
    void *symbol;

    if (slot_size != sizeof(symbol)) {
        nds_acl_set_error(api, "function-pointer size for %s differs from dlsym result size", name);
        return -1;
    }
    (void)dlerror();
    symbol = dlsym(api->library, name);
    loader_error = dlerror();
    if (loader_error != nullptr || symbol == nullptr) {
        nds_acl_set_error(api, "required symbol %s is unavailable: %s", name,
                          loader_error == nullptr ? "returned null" : loader_error);
        return -1;
    }
    memcpy(slot, &symbol, slot_size);
    return 0;
}
#endif

int nds_acl_open(NdsAclApi *api, const char *library_path) {
    if (api == nullptr) {
        return -1;
    }

#ifdef NDS_LINK_PUBLIC_ACL
    (void)library_path;
    nds_acl_close(api);
    api->library = (void *)&nds_linked_acl_library_marker;
    api->init = nds_linked_acl_init;
    api->finalize = nds_linked_acl_finalize;
    api->set_device = nds_linked_acl_set_device;
    api->get_phy_dev_id = nds_linked_acl_get_phy_dev_id;
    api->malloc_device = nds_linked_acl_malloc_device;
    api->free_device = nds_linked_acl_free_device;
    api->host_register = nds_linked_acl_host_register;
    api->host_unregister = nds_linked_acl_host_unregister;
    api->memcpy = nds_linked_acl_memcpy;
    api->memset_device = nds_linked_acl_memset_device;
    api->binary_load_from_file = nds_linked_acl_binary_load_from_file;
    api->binary_unload = nds_linked_acl_binary_unload;
    api->binary_get_function = nds_linked_acl_binary_get_function;
    api->launch_kernel_with_host_args = nds_linked_acl_launch_kernel_with_host_args;
    api->create_stream = nds_linked_acl_create_stream;
    api->create_stream_with_config = nds_linked_acl_create_stream_with_config;
    api->destroy_stream = nds_linked_acl_destroy_stream;
    api->synchronize_stream_with_timeout = nds_linked_acl_synchronize_stream_with_timeout;
    api->error[0] = '\0';
    return 0;
#else
    const char *loader_error;

    if (library_path == nullptr || library_path[0] == '\0') {
        nds_acl_set_error(api, "a non-empty AscendCL library path is required");
        return -1;
    }

    nds_acl_close(api);
    api->library = dlopen(library_path, RTLD_NOW | RTLD_LOCAL);
    if (api->library == nullptr) {
        loader_error = dlerror();
        nds_acl_set_error(api, "unable to load %s: %s", library_path,
                          loader_error == nullptr ? "unknown dynamic-loader error" : loader_error);
        return -1;
    }

#define NDS_ACL_RESOLVE(field, symbol)                                            \
    do {                                                                          \
        if (nds_acl_resolve(api, symbol, &api->field, sizeof(api->field)) != 0) { \
            nds_acl_close(api);                                                   \
            return -1;                                                            \
        }                                                                         \
    } while (0)

    NDS_ACL_RESOLVE(init, "aclInit");
    NDS_ACL_RESOLVE(finalize, "aclFinalize");
    NDS_ACL_RESOLVE(set_device, "aclrtSetDevice");
    NDS_ACL_RESOLVE(get_phy_dev_id, "aclrtGetPhyDevIdByLogicDevId");
    NDS_ACL_RESOLVE(malloc_device, "aclrtMalloc");
    NDS_ACL_RESOLVE(free_device, "aclrtFree");
    /* Page-locked host memory is optional; its absence must not block device-only clients. */
    (void)nds_acl_resolve(api, "aclrtHostRegister", &api->host_register, sizeof(api->host_register));
    if (api->host_register == nullptr)
        api->error[0] = '\0';
    (void)nds_acl_resolve(api, "aclrtHostUnregister", &api->host_unregister, sizeof(api->host_unregister));
    if (api->host_unregister == nullptr)
        api->error[0] = '\0';
    NDS_ACL_RESOLVE(memcpy, "aclrtMemcpy");
    NDS_ACL_RESOLVE(memset_device, "aclrtMemset");
    NDS_ACL_RESOLVE(binary_load_from_file, "aclrtBinaryLoadFromFile");
    NDS_ACL_RESOLVE(binary_unload, "aclrtBinaryUnLoad");
    NDS_ACL_RESOLVE(binary_get_function, "aclrtBinaryGetFunction");
    NDS_ACL_RESOLVE(launch_kernel_with_host_args, "aclrtLaunchKernelWithHostArgs");
    NDS_ACL_RESOLVE(create_stream, "aclrtCreateStream");
    NDS_ACL_RESOLVE(create_stream_with_config, "aclrtCreateStreamWithConfig");
    NDS_ACL_RESOLVE(destroy_stream, "aclrtDestroyStream");
    NDS_ACL_RESOLVE(synchronize_stream_with_timeout, "aclrtSynchronizeStreamWithTimeout");

#undef NDS_ACL_RESOLVE
    api->error[0] = '\0';
    return 0;
#endif
}

void nds_acl_close(NdsAclApi *api) {
    if (api == nullptr) {
        return;
    }
#ifndef NDS_LINK_PUBLIC_ACL
    if (api->library != nullptr) {
        (void)dlclose(api->library);
    }
#endif
    *api = {};
}

const char *nds_acl_error(const NdsAclApi *api) {
    return api == nullptr ? "no loader state" : api->error;
}
