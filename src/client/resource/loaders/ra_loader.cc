#include "nds/ra_loader.h"

#include "shared_library.hh"

#include <cstdio>

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

int nds_ra_open(NdsRaApi *api, const char *library_path) {
    if (api == nullptr)
        return -1;

    nds_ra_close(api);
    auto library = nds::client::SharedLibrary::open(library_path == nullptr ? "" : library_path);
    if (!library) {
        (void)std::snprintf(api->error, sizeof(api->error), "%s", library.error().message.c_str());
        return -1;
    }
#define NDS_RESOLVE(field, symbol)                                                                       \
    do {                                                                                                 \
        const auto resolved = library->resolve_required<decltype(api->field)>(symbol);                   \
        if (!resolved) {                                                                                 \
            (void)std::snprintf(api->error, sizeof(api->error), "%s", resolved.error().message.c_str()); \
            return -1;                                                                                   \
        }                                                                                                \
        api->field = *resolved;                                                                          \
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
    api->ra_ai_qp_create = library->resolve_optional<decltype(api->ra_ai_qp_create)>("RaAiQpCreate");
    api->ra_set_qp_attr_qos = library->resolve_optional<decltype(api->ra_set_qp_attr_qos)>("RaSetQpAttrQos");
    api->ra_set_qp_attr_timeout =
        library->resolve_optional<decltype(api->ra_set_qp_attr_timeout)>("RaSetQpAttrTimeout");
    api->ra_set_qp_attr_retry_count =
        library->resolve_optional<decltype(api->ra_set_qp_attr_retry_count)>("RaSetQpAttrRetryCnt");
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

    api->library = library->release();
    return 0;
}

void nds_ra_close(NdsRaApi *api) {
    if (api == nullptr)
        return;
    nds::client::SharedLibrary library(api->library);
    library.close();
    *api = {};
}

const char *nds_ra_error(const NdsRaApi *api) {
    return api == nullptr ? "no loader state" : api->error;
}
