#include "nds/runtime_loader.h"

#include <dlfcn.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static_assert(sizeof(nds_rt_proc_ext_param) == 16, "unexpected rtProcExtParam ABI layout");
static_assert(sizeof(nds_rt_net_service_open_args) == 16, "unexpected rtNetServiceOpenArgs ABI layout");

static void nds_runtime_set_error(nds_runtime_api *api, const char *format, ...)
{
    va_list arguments;

    va_start(arguments, format);
    (void)vsnprintf(api->error, sizeof(api->error), format, arguments);
    va_end(arguments);
}

static int nds_runtime_resolve(nds_runtime_api *api, const char *name, void *slot, size_t slot_size)
{
    const char *loader_error;
    void *symbol;

    if (slot_size != sizeof(symbol)) {
        nds_runtime_set_error(api, "function-pointer size for %s differs from dlsym result size", name);
        return -1;
    }
    (void)dlerror();
    symbol = dlsym(api->library, name);
    loader_error = dlerror();
    if (loader_error != nullptr || symbol == nullptr) {
        nds_runtime_set_error(api, "required symbol %s is unavailable: %s", name,
                              loader_error == nullptr ? "returned null" : loader_error);
        return -1;
    }
    memcpy(slot, &symbol, slot_size);
    return 0;
}

int nds_runtime_open(nds_runtime_api *api, const char *library_path)
{
    const char *loader_error;

    if (api == nullptr || library_path == nullptr || library_path[0] == '\0') {
        if (api != nullptr) {
            nds_runtime_set_error(api, "an nds_runtime_api and a non-empty library path are required");
        }
        return -1;
    }

    nds_runtime_close(api);
    api->library = dlopen(library_path, RTLD_NOW | RTLD_LOCAL);
    if (api->library == nullptr) {
        loader_error = dlerror();
        nds_runtime_set_error(api, "unable to load %s: %s", library_path,
                              loader_error == nullptr ? "unknown dynamic-loader error" : loader_error);
        return -1;
    }

#define NDS_RUNTIME_RESOLVE(field, symbol) \
    do { \
        if (nds_runtime_resolve(api, symbol, &api->field, sizeof(api->field)) != 0) { \
            nds_runtime_close(api); \
            return -1; \
        } \
    } while (0)

    NDS_RUNTIME_RESOLVE(set_device, "rtSetDevice");
    NDS_RUNTIME_RESOLVE(open_net_service, "rtOpenNetService");
    NDS_RUNTIME_RESOLVE(close_net_service, "rtCloseNetService");
    NDS_RUNTIME_RESOLVE(rdma_db_send, "rtRDMADBSend");

#undef NDS_RUNTIME_RESOLVE
    api->error[0] = '\0';
    return 0;
}

void nds_runtime_close(nds_runtime_api *api)
{
    if (api == nullptr) {
        return;
    }
    if (api->library != nullptr) {
        (void)dlclose(api->library);
    }
    *api = {};
}

const char *nds_runtime_error(const nds_runtime_api *api)
{
    return api == nullptr ? "no loader state" : api->error;
}
