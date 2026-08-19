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
typedef struct nds_rt_proc_ext_param {
    const char *param_info;
    uint64_t param_len;
} nds_rt_proc_ext_param;

typedef struct nds_rt_net_service_open_args {
    nds_rt_proc_ext_param *ext_param_list;
    uint64_t ext_param_count;
} nds_rt_net_service_open_args;

typedef int (*nds_rt_set_device_fn)(int32_t logical_device_id);
typedef int (*nds_rt_open_net_service_fn)(const nds_rt_net_service_open_args *args);
typedef int (*nds_rt_close_net_service_fn)(void);
/* rtRDMADBSend queues an OPBASE RA-posted WQE on the selected runtime stream. */
typedef int (*nds_rt_rdma_db_send_fn)(uint32_t db_index, uint64_t db_info, void *stream);

enum { NDS_RUNTIME_ERROR_CAPACITY = 512 };
enum { NDS_RUNTIME_HDC_SERVICE_TYPE_RDMA_V2 = 18 };

typedef struct nds_runtime_api {
    void *library;
    nds_rt_set_device_fn set_device;
    nds_rt_open_net_service_fn open_net_service;
    nds_rt_close_net_service_fn close_net_service;
    nds_rt_rdma_db_send_fn rdma_db_send;
    char error[NDS_RUNTIME_ERROR_CAPACITY];
} nds_runtime_api;

int nds_runtime_open(nds_runtime_api *api, const char *library_path);
void nds_runtime_close(nds_runtime_api *api);
const char *nds_runtime_error(const nds_runtime_api *api);

#ifdef __cplusplus
}
#endif

#endif
