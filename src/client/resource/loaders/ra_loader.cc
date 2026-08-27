#include "ra_loader.hh"

#include "shared_library.hh"

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

nds::Result<NdsRaApi> nds_ra_open(std::string_view library_path) {
    auto library = nds::client::SharedLibrary::open(library_path);
    if (!library)
        return nds::unexpected(library.error());
    NdsRaApi api{};
    NDS_RETURN_IF_ERROR(library->resolve_required("RaInit", &api.ra_init));
    NDS_RETURN_IF_ERROR(library->resolve_required("RaDeinit", &api.ra_deinit));
    NDS_RETURN_IF_ERROR(library->resolve_required("RaRdevInit", &api.ra_rdev_init));
    NDS_RETURN_IF_ERROR(library->resolve_required("RaRdevInitV2", &api.ra_rdev_init_v2));
    NDS_RETURN_IF_ERROR(library->resolve_required("RaRdevDeinit", &api.ra_rdev_deinit));
    NDS_RETURN_IF_ERROR(library->resolve_required("RaRdevGetPortStatus", &api.ra_rdev_get_port_status));
    NDS_RETURN_IF_ERROR(library->resolve_required("RaRdevGetSupportLite", &api.ra_rdev_get_support_lite));
    NDS_RETURN_IF_ERROR(library->resolve_required("RaQpCreate", &api.ra_qp_create));
    NDS_RETURN_IF_ERROR(library->resolve_required("RaQpConnectAsync", &api.ra_qp_connect_async));
    NDS_RETURN_IF_ERROR(library->resolve_required("RaTypicalQpCreate", &api.ra_typical_qp_create));
    NDS_RETURN_IF_ERROR(library->resolve_required("RaTypicalQpModify", &api.ra_typical_qp_modify));
    NDS_RETURN_IF_ERROR(library->resolve_required("RaQpDestroy", &api.ra_qp_destroy));
    NDS_RETURN_IF_ERROR(library->resolve_required("RaGetQpAttr", &api.ra_get_qp_attr));
    NDS_RETURN_IF_ERROR(library->resolve_required("RaGetQpStatus", &api.ra_get_qp_status));
    NDS_RETURN_IF_ERROR(library->resolve_required("RaRdevGetCqeErrInfoList", &api.ra_rdev_get_cqe_error_list));
    NDS_RETURN_IF_ERROR(library->resolve_required("RaRegisterMr", &api.ra_register_mr));
    NDS_RETURN_IF_ERROR(library->resolve_required("RaDeregisterMr", &api.ra_deregister_mr));
    NDS_RETURN_IF_ERROR(library->resolve_required("RaTypicalSendWr", &api.ra_typical_send_wr));
    NDS_RETURN_IF_ERROR(library->resolve_required("RaRecvWrlist", &api.ra_recv_wrlist));
    NDS_RETURN_IF_ERROR(library->resolve_required("RaPollCq", &api.ra_poll_cq));
    NDS_RETURN_IF_ERROR(library->resolve_required("RaGetIfnum", &api.ra_get_interface_count));
    NDS_RETURN_IF_ERROR(library->resolve_required("RaGetIfaddrs", &api.ra_get_interfaces));
    library->resolve_optional("RaAiQpCreate", &api.ra_ai_qp_create);
    library->resolve_optional("RaSetQpAttrQos", &api.ra_set_qp_attr_qos);
    library->resolve_optional("RaSetQpAttrTimeout", &api.ra_set_qp_attr_timeout);
    library->resolve_optional("RaSetQpAttrRetryCnt", &api.ra_set_qp_attr_retry_count);
    api.library = library->release();
    return api;
}

void nds_ra_close(NdsRaApi *api) {
    if (api == nullptr)
        return;
    nds::client::SharedLibrary library(api->library);
    library.close();
    *api = {};
}
