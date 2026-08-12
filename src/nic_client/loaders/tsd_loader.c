#include "nds/tsd_loader.h"

#include <dlfcn.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static void nds_tsd_set_error(nds_tsd_api *api, const char *format, ...)
{
    va_list arguments;

    va_start(arguments, format);
    (void)vsnprintf(api->error, sizeof(api->error), format, arguments);
    va_end(arguments);
}

static int nds_tsd_resolve(nds_tsd_api *api, const char *name, void *slot, size_t slot_size)
{
    const char *loader_error;
    void *symbol;

    if (slot_size != sizeof(symbol)) {
        nds_tsd_set_error(api, "function-pointer size for %s differs from dlsym result size", name);
        return -1;
    }
    (void)dlerror();
    symbol = dlsym(api->library, name);
    loader_error = dlerror();
    if (loader_error != NULL || symbol == NULL) {
        nds_tsd_set_error(api, "required symbol %s is unavailable: %s", name,
                          loader_error == NULL ? "returned null" : loader_error);
        return -1;
    }
    memcpy(slot, &symbol, slot_size);
    return 0;
}

int nds_tsd_open_library(nds_tsd_api *api, const char *library_path)
{
    const char *loader_error;

    if (api == NULL || library_path == NULL || library_path[0] == '\0') {
        if (api != NULL) {
            nds_tsd_set_error(api, "an nds_tsd_api and a non-empty library path are required");
        }
        return -1;
    }

    nds_tsd_close_library(api);
    api->library = dlopen(library_path, RTLD_NOW | RTLD_LOCAL);
    if (api->library == NULL) {
        loader_error = dlerror();
        nds_tsd_set_error(api, "unable to load %s: %s", library_path,
                          loader_error == NULL ? "unknown dynamic-loader error" : loader_error);
        return -1;
    }

#define NDS_TSD_RESOLVE(field, symbol) \
    do { \
        if (nds_tsd_resolve(api, symbol, &api->field, sizeof(api->field)) != 0) { \
            nds_tsd_close_library(api); \
            return -1; \
        } \
    } while (0)

    NDS_TSD_RESOLVE(open, "TsdOpen");
    NDS_TSD_RESOLVE(close, "TsdClose");
    NDS_TSD_RESOLVE(capability_get, "TsdCapabilityGet");

#undef NDS_TSD_RESOLVE
    api->error[0] = '\0';
    return 0;
}

void nds_tsd_close_library(nds_tsd_api *api)
{
    if (api == NULL) {
        return;
    }
    if (api->library != NULL) {
        (void)dlclose(api->library);
    }
    memset(api, 0, sizeof(*api));
}

const char *nds_tsd_error(const nds_tsd_api *api)
{
    return api == NULL ? "no loader state" : api->error;
}
