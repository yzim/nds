#include "nds/runtime_loader.h"

#include "shared_library.hh"

#include <cstdio>

static_assert(sizeof(NdsRtProcExtParam) == 16, "unexpected rtProcExtParam ABI layout");
static_assert(sizeof(NdsRtNetServiceOpenArgs) == 16, "unexpected rtNetServiceOpenArgs ABI layout");

int nds_runtime_open(NdsRuntimeApi *api, const char *library_path) {
    if (api == nullptr)
        return -1;

    nds_runtime_close(api);
    auto library = nds::client::SharedLibrary::open(library_path == nullptr ? "" : library_path);
    if (!library) {
        (void)std::snprintf(api->error, sizeof(api->error), "%s", library.error().message.c_str());
        return -1;
    }
#define NDS_RUNTIME_RESOLVE(field, symbol)                                                               \
    do {                                                                                                 \
        const auto resolved = library->resolve_required<decltype(api->field)>(symbol);                   \
        if (!resolved) {                                                                                 \
            (void)std::snprintf(api->error, sizeof(api->error), "%s", resolved.error().message.c_str()); \
            return -1;                                                                                   \
        }                                                                                                \
        api->field = *resolved;                                                                          \
    } while (0)

    NDS_RUNTIME_RESOLVE(set_device, "rtSetDevice");
    NDS_RUNTIME_RESOLVE(open_net_service, "rtOpenNetService");
    NDS_RUNTIME_RESOLVE(close_net_service, "rtCloseNetService");
    NDS_RUNTIME_RESOLVE(rdma_db_send, "rtRDMADBSend");
#undef NDS_RUNTIME_RESOLVE
    api->library = library->release();
    return 0;
}

void nds_runtime_close(NdsRuntimeApi *api) {
    if (api == nullptr)
        return;
    nds::client::SharedLibrary library(api->library);
    library.close();
    *api = {};
}

const char *nds_runtime_error(const NdsRuntimeApi *api) {
    return api == nullptr ? "no loader state" : api->error;
}
