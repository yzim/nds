#ifndef NDS_RUNTIME_LOADER_H
#define NDS_RUNTIME_LOADER_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

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

enum { NDS_RUNTIME_ERROR_CAPACITY = 512 };
enum { NDS_RUNTIME_HDC_SERVICE_TYPE_RDMA_V2 = 18 };

typedef struct NdsRuntimeApi {
    void *library;
    NdsRtSetDeviceFn set_device;
    NdsRtOpenNetServiceFn open_net_service;
    NdsRtCloseNetServiceFn close_net_service;
    NdsRtRdmaDbSendFn rdma_db_send;
    char error[NDS_RUNTIME_ERROR_CAPACITY];
} NdsRuntimeApi;

int nds_runtime_open(NdsRuntimeApi *api, const char *library_path);
void nds_runtime_close(NdsRuntimeApi *api);
const char *nds_runtime_error(const NdsRuntimeApi *api);

#ifdef __cplusplus
}
#endif

#endif
