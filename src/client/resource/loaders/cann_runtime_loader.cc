#include "cann_runtime_loader.hh"

#include "shared_library.hh"

static_assert(sizeof(NdsRtProcExtParam) == 16, "unexpected rtProcExtParam ABI layout");
static_assert(sizeof(NdsRtNetServiceOpenArgs) == 16, "unexpected rtNetServiceOpenArgs ABI layout");

nds::Result<NdsCannRuntimeApi> nds_cann_runtime_open(std::string_view library_path) {
    auto library = nds::client::SharedLibrary::open(library_path);
    if (!library)
        return nds::unexpected(library.error());
    NdsCannRuntimeApi api{};
    NDS_RETURN_IF_ERROR(library->resolve_required("rtSetDevice", &api.set_device));
    NDS_RETURN_IF_ERROR(library->resolve_required("rtOpenNetService", &api.open_net_service));
    NDS_RETURN_IF_ERROR(library->resolve_required("rtCloseNetService", &api.close_net_service));
    NDS_RETURN_IF_ERROR(library->resolve_required("rtRDMADBSend", &api.rdma_db_send));
    api.library = library->release();
    return api;
}

void nds_cann_runtime_close(NdsCannRuntimeApi *api) {
    if (api == nullptr)
        return;
    nds::client::SharedLibrary library(api->library);
    library.close();
    *api = {};
}
