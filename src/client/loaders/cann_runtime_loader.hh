#ifndef NDS_CLIENT_LOADERS_CANN_RUNTIME_LOADER_HH
#define NDS_CLIENT_LOADERS_CANN_RUNTIME_LOADER_HH

#include "result.hh"

#include <stddef.h>
#include <stdint.h>

#include <string_view>

/*
 * Minimal ABI surface used to ask the installed CANN runtime to start the
 * device-side network service. These structures are not sent over the wire.
 * They are independently transcribed from CANN's runtime/rts/rts_device.h.
 */
typedef struct NdsRtProcExtParam {
    const char *param_info;
    uint64_t param_len;
} NdsRtProcExtParam;

typedef struct NdsRtNetServiceOpenArgs {
    NdsRtProcExtParam *ext_param_list;
    uint64_t ext_param_count;
} NdsRtNetServiceOpenArgs;

typedef int (*NdsRtSetDeviceFn)(int32_t logical_device_id);
typedef int (*NdsRtOpenNetServiceFn)(const NdsRtNetServiceOpenArgs *args);
typedef int (*NdsRtCloseNetServiceFn)(void);
/* rtRDMADBSend queues an OPBASE RA-posted WQE on the selected runtime stream. */
typedef int (*NdsRtRdmaDbSendFn)(uint32_t db_index, uint64_t db_info, void *stream);

enum { NDS_RUNTIME_HDC_SERVICE_TYPE_RDMA_V2 = 18 };

typedef struct NdsCannRuntimeApi {
    void *library;
    NdsRtSetDeviceFn set_device;
    NdsRtOpenNetServiceFn open_net_service;
    NdsRtCloseNetServiceFn close_net_service;
    NdsRtRdmaDbSendFn rdma_db_send;
} NdsCannRuntimeApi;

nds::Result<NdsCannRuntimeApi> nds_cann_runtime_open(std::string_view library_path);
void nds_cann_runtime_close(NdsCannRuntimeApi *api);

#endif
