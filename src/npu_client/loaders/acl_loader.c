#include "nds/acl_loader.h"

#include <dlfcn.h>
#ifdef NDS_LINK_PUBLIC_ACL
#include <acl/acl.h>
#endif
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#ifdef NDS_LINK_PUBLIC_ACL
/* Public AscendCL ABI wrappers keep nds_acl_api's stable int-returning surface. */
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
    api->library = (void *)&nds_linked_acl_init;
    api->init = nds_linked_acl_init;
    api->finalize = nds_linked_acl_finalize;
    api->set_device = nds_linked_acl_set_device;
    api->malloc_device = nds_linked_acl_malloc_device;
    api->free_device = nds_linked_acl_free_device;
    api->memcpy = nds_linked_acl_memcpy;
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
