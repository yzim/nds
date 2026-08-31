#ifndef NDS_CLIENT_LOADERS_LIBRUNTIME_HH
#define NDS_CLIENT_LOADERS_LIBRUNTIME_HH

#include "result.hh"
#include "shared_library.hh"

#include <stddef.h>
#include <stdint.h>

#include <string_view>

namespace nds::client {
struct RuntimeTestAccess;
}

/* Move-only owner of libruntime.so and its resolved ABI symbols. */
class Libruntime {
public:
    /* CANN runtime/rts/rts_device.h ABI value. */
    enum { HDC_SERVICE_TYPE_RDMA_V2 = 18 };

    struct ProcExtParam {
        const char *param_info;
        uint64_t param_len;
    };

    struct NetServiceOpenArgs {
        ProcExtParam *ext_param_list;
        uint64_t ext_param_count;
    };

    using SetDeviceFn = int (*)(int32_t logical_device_id);
    using OpenNetServiceFn = int (*)(const NetServiceOpenArgs *args);
    using CloseNetServiceFn = int (*)(void);
    /* rtRDMADBSend queues an OPBASE RA-posted WQE on the selected stream. */
    using RdmaDbSendFn = int (*)(uint32_t db_index, uint64_t db_info, void *stream);

    Libruntime() = default;
    ~Libruntime() = default;
    Libruntime(const Libruntime &) = delete;
    Libruntime &operator=(const Libruntime &) = delete;
    Libruntime(Libruntime &&) noexcept = default;
    Libruntime &operator=(Libruntime &&) noexcept = default;

    /* Production loads the CANN-exported SONAME; tests may supply a fake path. */
    static nds::Result<Libruntime> open(std::string_view library_path = "libruntime.so");

    SetDeviceFn set_device{};
    OpenNetServiceFn open_net_service{};
    CloseNetServiceFn close_net_service{};
    RdmaDbSendFn rdma_db_send{};

private:
    nds::client::SharedLibrary library_;
};

#endif
