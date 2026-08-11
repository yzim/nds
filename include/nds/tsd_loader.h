#ifndef NDS_TSD_LOADER_H
#define NDS_TSD_LOADER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Minimal ABI from CANN's pkg_inc/aicpu/tsd/tsd_client.h.  The rank-size
 * contract is documented there: values greater than one cause TSD to pull
 * HCCP for communication work.  This remains dynamically loaded.
 */
typedef uint32_t (*nds_tsd_open_fn)(uint32_t logical_device_id, uint32_t rank_size);
typedef uint32_t (*nds_tsd_close_fn)(uint32_t logical_device_id);
typedef uint32_t (*nds_tsd_capability_get_fn)(uint32_t logical_device_id, int32_t type, uint64_t result_pointer);

enum {
    NDS_TSD_CAPABILITY_MULTIPLE_HCCP = 6,
    NDS_TSD_ERROR_CAPACITY = 512,
};

typedef struct nds_tsd_api {
    void *library;
    nds_tsd_open_fn open;
    nds_tsd_close_fn close;
    nds_tsd_capability_get_fn capability_get;
    char error[NDS_TSD_ERROR_CAPACITY];
} nds_tsd_api;

int nds_tsd_open_library(nds_tsd_api *api, const char *library_path);
void nds_tsd_close_library(nds_tsd_api *api);
const char *nds_tsd_error(const nds_tsd_api *api);

#ifdef __cplusplus
}
#endif

#endif
