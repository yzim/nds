#include "nds/hcomm_loader.h"

#include <dlfcn.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static void nds_hcomm_set_error(nds_hcomm_api *api, const char *format, ...)
{
    va_list arguments;

    va_start(arguments, format);
    (void)vsnprintf(api->error, sizeof(api->error), format, arguments);
    va_end(arguments);
}

static int nds_hcomm_resolve(nds_hcomm_api *api, const char *name, void *slot, size_t slot_size)
{
    const char *loader_error;
    void *symbol;

    if (slot_size != sizeof(symbol)) {
        nds_hcomm_set_error(api, "function-pointer size for %s differs from dlsym result size", name);
        return -1;
    }
    (void)dlerror();
    symbol = dlsym(api->library, name);
    loader_error = dlerror();
    if (loader_error != NULL || symbol == NULL) {
        nds_hcomm_set_error(api, "required symbol %s is unavailable: %s", name,
                            loader_error == NULL ? "returned null" : loader_error);
        return -1;
    }
    memcpy(slot, &symbol, slot_size);
    return 0;
}

int nds_hcomm_open(nds_hcomm_api *api, const char *library_path)
{
    const char *loader_error;

    if (api == NULL || library_path == NULL || library_path[0] == '\0') {
        if (api != NULL) {
            nds_hcomm_set_error(api, "an nds_hcomm_api and a non-empty library path are required");
        }
        return -1;
    }

    nds_hcomm_close(api);
    api->library = dlopen(library_path, RTLD_NOW | RTLD_LOCAL);
    if (api->library == NULL) {
        loader_error = dlerror();
        nds_hcomm_set_error(api, "unable to load %s: %s", library_path,
                            loader_error == NULL ? "unknown dynamic-loader error" : loader_error);
        return -1;
    }

#define NDS_HCOMM_RESOLVE(field, symbol) \
    do { \
        if (nds_hcomm_resolve(api, symbol, &api->field, sizeof(api->field)) != 0) { \
            nds_hcomm_close(api); \
            return -1; \
        } \
    } while (0)

    NDS_HCOMM_RESOLVE(init_by_file, "HcomInitByFile");
    NDS_HCOMM_RESOLVE(destroy, "HcomDestroy");

#undef NDS_HCOMM_RESOLVE
    api->error[0] = '\0';
    return 0;
}

void nds_hcomm_close(nds_hcomm_api *api)
{
    if (api == NULL) {
        return;
    }
    if (api->library != NULL) {
        (void)dlclose(api->library);
    }
    memset(api, 0, sizeof(*api));
}

const char *nds_hcomm_error(const nds_hcomm_api *api)
{
    return api == NULL ? "no loader state" : api->error;
}
