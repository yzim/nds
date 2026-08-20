#include "nds/ra_loader.h"

#include <dlfcn.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static_assert(sizeof(NdsRaInitConfig) == 16, "unexpected RaInitConfig ABI layout");
static_assert(sizeof(NdsRaRdev) == 24, "unexpected rdev ABI layout");
static_assert(sizeof(NdsRaRdevInitInfo) == 12, "unexpected RdevInitInfo ABI layout");
static_assert(sizeof(NdsRaMrInfo) == 32, "unexpected MrInfoT ABI layout");
static_assert(sizeof(NdsRaSge) == 16, "unexpected SgList ABI layout");
static_assert(sizeof(NdsRaSendWr) == 40, "unexpected SendWr ABI layout");
static_assert(sizeof(NdsRaRecvWr) == 24, "unexpected RecvWrlistData ABI layout");
static_assert(sizeof(NdsRaSendResponse) == 16, "unexpected SendWrRsp ABI layout");
static_assert(sizeof(NdsRaCqeError) == 24, "unexpected CqeErrInfo ABI layout");
static_assert(sizeof(NdsRaCompletion) == 56, "unexpected rdma_lite_wc_v2 ABI layout");
static_assert(sizeof(NdsRaQpAttr) == 100, "unexpected QpAttr ABI layout");
static_assert(sizeof(NdsRaTypicalQp) == 184, "unexpected TypicalQp ABI layout");
static_assert(sizeof(NdsRaQpInitAttr) == 64, "unexpected ibv_qp_init_attr ABI layout");
static_assert(sizeof(NdsRaQpExtAttrs) == 224, "unexpected QpExtAttrs ABI layout");
static_assert(sizeof(NdsRaAiQpInfo) == 368, "unexpected AiQpInfo ABI layout");

static void nds_ra_set_error(NdsRaApi *api, const char *format, ...) {
    va_list arguments;

    va_start(arguments, format);
    (void)vsnprintf(api->error, sizeof(api->error), format, arguments);
    va_end(arguments);
}

static int nds_ra_resolve(NdsRaApi *api, const char *name, void *slot, size_t slot_size) {
    const char *loader_error;
    void *symbol;

    if (slot_size != sizeof(symbol)) {
        nds_ra_set_error(api, "function-pointer size for %s differs from dlsym result size", name);
        return -1;
    }
    (void)dlerror();
    symbol = dlsym(api->library, name);
    loader_error = dlerror();
    if (loader_error != nullptr || symbol == nullptr) {
        nds_ra_set_error(api, "required symbol %s is unavailable: %s", name,
                         loader_error == nullptr ? "returned null" : loader_error);
        return -1;
    }
    memcpy(slot, &symbol, slot_size);
    return 0;
}

static void nds_ra_resolve_optional(NdsRaApi *api, const char *name, void *slot, size_t slot_size) {
    const char *loader_error;
    void *symbol;

    if (slot_size != sizeof(symbol)) {
        return;
    }
    (void)dlerror();
    symbol = dlsym(api->library, name);
    loader_error = dlerror();
    if (loader_error == nullptr && symbol != nullptr) {
        memcpy(slot, &symbol, slot_size);
    }
}

int nds_ra_open(NdsRaApi *api, const char *library_path) {
    const char *loader_error;

    if (api == nullptr || library_path == nullptr || library_path[0] == '\0') {
        if (api != nullptr) {
            nds_ra_set_error(api, "loader state and a non-empty library path are required");
        }
        return -1;
    }

    nds_ra_close(api);
    api->library = dlopen(library_path, RTLD_NOW | RTLD_LOCAL);
    if (api->library == nullptr) {
        loader_error = dlerror();
        nds_ra_set_error(api, "unable to load %s: %s", library_path,
                         loader_error == nullptr ? "unknown dynamic-loader error" : loader_error);
        return -1;
    }

#define NDS_RESOLVE(field, symbol)                                               \
    do {                                                                         \
        if (nds_ra_resolve(api, symbol, &api->field, sizeof(api->field)) != 0) { \
            nds_ra_close(api);                                                   \
            return -1;                                                           \
        }                                                                        \
    } while (0)

    NDS_RESOLVE(ra_init, "RaInit");
    NDS_RESOLVE(ra_deinit, "RaDeinit");
    NDS_RESOLVE(ra_rdev_init, "RaRdevInit");
    NDS_RESOLVE(ra_rdev_init_v2, "RaRdevInitV2");
    NDS_RESOLVE(ra_rdev_deinit, "RaRdevDeinit");
    NDS_RESOLVE(ra_rdev_get_port_status, "RaRdevGetPortStatus");
    NDS_RESOLVE(ra_rdev_get_support_lite, "RaRdevGetSupportLite");
    NDS_RESOLVE(ra_qp_create, "RaQpCreate");
    NDS_RESOLVE(ra_qp_connect_async, "RaQpConnectAsync");
    NDS_RESOLVE(ra_typical_qp_create, "RaTypicalQpCreate");
    nds_ra_resolve_optional(api, "RaAiQpCreate", &api->ra_ai_qp_create, sizeof(api->ra_ai_qp_create));
    nds_ra_resolve_optional(api, "RaSetQpAttrQos", &api->ra_set_qp_attr_qos, sizeof(api->ra_set_qp_attr_qos));
    nds_ra_resolve_optional(api, "RaSetQpAttrTimeout", &api->ra_set_qp_attr_timeout,
                            sizeof(api->ra_set_qp_attr_timeout));
    nds_ra_resolve_optional(api, "RaSetQpAttrRetryCnt", &api->ra_set_qp_attr_retry_count,
                            sizeof(api->ra_set_qp_attr_retry_count));
    NDS_RESOLVE(ra_typical_qp_modify, "RaTypicalQpModify");
    NDS_RESOLVE(ra_qp_destroy, "RaQpDestroy");
    NDS_RESOLVE(ra_get_qp_attr, "RaGetQpAttr");
    NDS_RESOLVE(ra_get_qp_status, "RaGetQpStatus");
    NDS_RESOLVE(ra_rdev_get_cqe_error_list, "RaRdevGetCqeErrInfoList");
    NDS_RESOLVE(ra_register_mr, "RaRegisterMr");
    NDS_RESOLVE(ra_deregister_mr, "RaDeregisterMr");
    NDS_RESOLVE(ra_typical_send_wr, "RaTypicalSendWr");
    NDS_RESOLVE(ra_recv_wrlist, "RaRecvWrlist");
    NDS_RESOLVE(ra_poll_cq, "RaPollCq");
    NDS_RESOLVE(ra_get_interface_count, "RaGetIfnum");
    NDS_RESOLVE(ra_get_interfaces, "RaGetIfaddrs");

#undef NDS_RESOLVE
    api->error[0] = '\0';
    return 0;
}

void nds_ra_close(NdsRaApi *api) {
    if (api == nullptr) {
        return;
    }
    if (api->library != nullptr) {
        (void)dlclose(api->library);
    }
    *api = {};
}

const char *nds_ra_error(const NdsRaApi *api) {
    return api == nullptr ? "no loader state" : api->error;
}
