#include "libra.hh"

#include "shared_library.hh"

static_assert(sizeof(Libra::InitConfig) == 16, "unexpected RaInitConfig ABI layout");
static_assert(sizeof(Libra::Rdev) == 24, "unexpected rdev ABI layout");
static_assert(sizeof(Libra::RdevInitInfo) == 12, "unexpected RdevInitInfo ABI layout");
static_assert(sizeof(Libra::MrInfo) == 32, "unexpected MrInfoT ABI layout");
static_assert(sizeof(Libra::Sge) == 16, "unexpected SgList ABI layout");
static_assert(sizeof(Libra::SendWr) == 40, "unexpected SendWr ABI layout");
static_assert(sizeof(Libra::RecvWr) == 24, "unexpected RecvWrlistData ABI layout");
static_assert(sizeof(Libra::SendResponse) == 16, "unexpected SendWrRsp ABI layout");
static_assert(sizeof(Libra::CqeError) == 24, "unexpected CqeErrInfo ABI layout");
static_assert(sizeof(Libra::Completion) == 56, "unexpected rdma_lite_wc_v2 ABI layout");
static_assert(sizeof(Libra::QpAttr) == 100, "unexpected QpAttr ABI layout");
static_assert(sizeof(Libra::TypicalQp) == 184, "unexpected TypicalQp ABI layout");
static_assert(sizeof(Libra::QpInitAttr) == 64, "unexpected ibv_qp_init_attr ABI layout");
static_assert(sizeof(Libra::QpExtAttrs) == 224, "unexpected QpExtAttrs ABI layout");
static_assert(sizeof(Libra::AiQpInfo) == 368, "unexpected AiQpInfo ABI layout");

nds::Result<Libra> Libra::open(std::string_view library_path) {
    NDS_ASSIGN_OR_RETURN(nds::client::SharedLibrary library, nds::client::SharedLibrary::open(library_path));
    Libra libra;
    NDS_RETURN_IF_ERROR(library.resolve_required("RaInit", &libra.init));
    NDS_RETURN_IF_ERROR(library.resolve_required("RaDeinit", &libra.deinit));
    NDS_RETURN_IF_ERROR(library.resolve_required("RaRdevInit", &libra.rdev_init));
    NDS_RETURN_IF_ERROR(library.resolve_required("RaRdevInitV2", &libra.rdev_init_v2));
    NDS_RETURN_IF_ERROR(library.resolve_required("RaRdevDeinit", &libra.rdev_deinit));
    NDS_RETURN_IF_ERROR(library.resolve_required("RaRdevGetPortStatus", &libra.rdev_get_port_status));
    NDS_RETURN_IF_ERROR(library.resolve_required("RaRdevGetSupportLite", &libra.rdev_get_support_lite));
    NDS_RETURN_IF_ERROR(library.resolve_required("RaQpCreate", &libra.qp_create));
    NDS_RETURN_IF_ERROR(library.resolve_required("RaQpConnectAsync", &libra.qp_connect_async));
    NDS_RETURN_IF_ERROR(library.resolve_required("RaTypicalQpCreate", &libra.typical_qp_create));
    NDS_RETURN_IF_ERROR(library.resolve_required("RaTypicalQpModify", &libra.typical_qp_modify));
    NDS_RETURN_IF_ERROR(library.resolve_required("RaQpDestroy", &libra.qp_destroy));
    NDS_RETURN_IF_ERROR(library.resolve_required("RaGetQpAttr", &libra.get_qp_attr));
    NDS_RETURN_IF_ERROR(library.resolve_required("RaGetQpStatus", &libra.get_qp_status));
    NDS_RETURN_IF_ERROR(library.resolve_required("RaRdevGetCqeErrInfoList", &libra.rdev_get_cqe_error_list));
    NDS_RETURN_IF_ERROR(library.resolve_required("RaRegisterMr", &libra.register_mr));
    NDS_RETURN_IF_ERROR(library.resolve_required("RaDeregisterMr", &libra.deregister_mr));
    NDS_RETURN_IF_ERROR(library.resolve_required("RaTypicalSendWr", &libra.typical_send_wr));
    NDS_RETURN_IF_ERROR(library.resolve_required("RaRecvWrlist", &libra.recv_wrlist));
    NDS_RETURN_IF_ERROR(library.resolve_required("RaPollCq", &libra.poll_cq));
    NDS_RETURN_IF_ERROR(library.resolve_required("RaGetIfnum", &libra.get_interface_count));
    NDS_RETURN_IF_ERROR(library.resolve_required("RaGetIfaddrs", &libra.get_interfaces));
    library.resolve_optional("RaAiQpCreate", &libra.ai_qp_create);
    library.resolve_optional("RaSetQpAttrQos", &libra.set_qp_attr_qos);
    library.resolve_optional("RaSetQpAttrTimeout", &libra.set_qp_attr_timeout);
    library.resolve_optional("RaSetQpAttrRetryCnt", &libra.set_qp_attr_retry_count);
    libra.library_ = std::move(library);
    return libra;
}
