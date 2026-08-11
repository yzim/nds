#include "nds/ra_loader.h"

#include <dlfcn.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

_Static_assert(sizeof(nds_ra_init_config) == 16, "unexpected RaInitConfig ABI layout");
_Static_assert(sizeof(nds_ra_rdev) == 24, "unexpected rdev ABI layout");
_Static_assert(sizeof(nds_ra_mr_info) == 32, "unexpected MrInfoT ABI layout");
_Static_assert(sizeof(nds_ra_sge) == 16, "unexpected SgList ABI layout");
_Static_assert(sizeof(nds_ra_send_wr) == 40, "unexpected SendWr ABI layout");
_Static_assert(sizeof(nds_ra_send_response) == 16, "unexpected SendWrRsp ABI layout");
_Static_assert(sizeof(nds_ra_cqe_error) == 24, "unexpected CqeErrInfo ABI layout");
_Static_assert(sizeof(nds_ra_completion) == 56, "unexpected rdma_lite_wc_v2 ABI layout");
_Static_assert(sizeof(nds_ra_qp_attr) == 100, "unexpected QpAttr ABI layout");
_Static_assert(sizeof(nds_ra_typical_qp) == 184, "unexpected TypicalQp ABI layout");

static void nds_ra_set_error(nds_ra_api *api, const char *format, ...)
{
    va_list arguments;

    va_start(arguments, format);
    (void)vsnprintf(api->error, sizeof(api->error), format, arguments);
    va_end(arguments);
}

static int nds_ra_resolve(nds_ra_api *api, const char *name, void *slot, size_t slot_size)
{
    const char *loader_error;
    void *symbol;

    if (slot_size != sizeof(symbol)) {
        nds_ra_set_error(api, "function-pointer size for %s differs from dlsym result size", name);
        return -1;
    }
    (void)dlerror();
    symbol = dlsym(api->library, name);
    loader_error = dlerror();
    if (loader_error != NULL || symbol == NULL) {
        nds_ra_set_error(api, "required symbol %s is unavailable: %s", name,
                         loader_error == NULL ? "returned null" : loader_error);
        return -1;
    }
    memcpy(slot, &symbol, slot_size);
    return 0;
}

int nds_ra_open(nds_ra_api *api, const char *library_path)
{
    const char *loader_error;

    if (api == NULL || library_path == NULL || library_path[0] == '\0') {
        if (api != NULL) {
            nds_ra_set_error(api, "an nds_ra_api and a non-empty library path are required");
        }
        return -1;
    }

    nds_ra_close(api);
    api->library = dlopen(library_path, RTLD_NOW | RTLD_LOCAL);
    if (api->library == NULL) {
        loader_error = dlerror();
        nds_ra_set_error(api, "unable to load %s: %s", library_path,
                         loader_error == NULL ? "unknown dynamic-loader error" : loader_error);
        return -1;
    }

#define NDS_RESOLVE(field, symbol) \
    do { \
        if (nds_ra_resolve(api, symbol, &api->field, sizeof(api->field)) != 0) { \
            nds_ra_close(api); \
            return -1; \
        } \
    } while (0)

    NDS_RESOLVE(ra_init, "RaInit");
    NDS_RESOLVE(ra_deinit, "RaDeinit");
    NDS_RESOLVE(ra_rdev_init, "RaRdevInit");
    NDS_RESOLVE(ra_rdev_deinit, "RaRdevDeinit");
    NDS_RESOLVE(ra_rdev_get_port_status, "RaRdevGetPortStatus");
    NDS_RESOLVE(ra_rdev_get_support_lite, "RaRdevGetSupportLite");
    NDS_RESOLVE(ra_qp_create, "RaQpCreate");
    NDS_RESOLVE(ra_qp_connect_async, "RaQpConnectAsync");
    NDS_RESOLVE(ra_typical_qp_create, "RaTypicalQpCreate");
    NDS_RESOLVE(ra_typical_qp_modify, "RaTypicalQpModify");
    NDS_RESOLVE(ra_qp_destroy, "RaQpDestroy");
    NDS_RESOLVE(ra_get_qp_attr, "RaGetQpAttr");
    NDS_RESOLVE(ra_get_qp_status, "RaGetQpStatus");
    NDS_RESOLVE(ra_rdev_get_cqe_error_list, "RaRdevGetCqeErrInfoList");
    NDS_RESOLVE(ra_register_mr, "RaRegisterMr");
    NDS_RESOLVE(ra_deregister_mr, "RaDeregisterMr");
    NDS_RESOLVE(ra_typical_send_wr, "RaTypicalSendWr");
    NDS_RESOLVE(ra_poll_cq, "RaPollCq");

#undef NDS_RESOLVE
    api->error[0] = '\0';
    return 0;
}

void nds_ra_close(nds_ra_api *api)
{
    if (api == NULL) {
        return;
    }
    if (api->library != NULL) {
        (void)dlclose(api->library);
    }
    memset(api, 0, sizeof(*api));
}

const char *nds_ra_error(const nds_ra_api *api)
{
    return api == NULL ? "no loader state" : api->error;
}
