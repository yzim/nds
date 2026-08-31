#include "libruntime.hh"

#include "shared_library.hh"

static_assert(sizeof(Libruntime::ProcExtParam) == 16, "unexpected rtProcExtParam ABI layout");
static_assert(sizeof(Libruntime::NetServiceOpenArgs) == 16, "unexpected rtNetServiceOpenArgs ABI layout");

nds::Result<Libruntime> Libruntime::open(std::string_view library_path) {
    NDS_ASSIGN_OR_RETURN(nds::client::SharedLibrary library, nds::client::SharedLibrary::open(library_path));
    Libruntime runtime;
    NDS_RETURN_IF_ERROR(library.resolve_required("rtSetDevice", &runtime.set_device));
    NDS_RETURN_IF_ERROR(library.resolve_required("rtOpenNetService", &runtime.open_net_service));
    NDS_RETURN_IF_ERROR(library.resolve_required("rtCloseNetService", &runtime.close_net_service));
    NDS_RETURN_IF_ERROR(library.resolve_required("rtRDMADBSend", &runtime.rdma_db_send));
    runtime.library_ = std::move(library);
    return runtime;
}
