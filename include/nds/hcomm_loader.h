#ifndef NDS_HCOMM_LOADER_H
#define NDS_HCOMM_LOADER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Minimal public HCOMM entry points dynamically resolved from libhcomm.so.
 * HcomInitByFile creates the process communicator from a CANN rank table;
 * for a communicator with more than one rank, HCOMM owns its internal HCCP/RA
 * initialization.  The return type is HcclResult (an int32_t ABI value).
 */
typedef int32_t (*nds_hcom_init_by_file_fn)(const char *rank_table_path, const char *identify);
typedef int32_t (*nds_hcom_destroy_fn)(void);

enum { NDS_HCOMM_ERROR_CAPACITY = 512 };

typedef struct nds_hcomm_api {
    void *library;
    nds_hcom_init_by_file_fn init_by_file;
    nds_hcom_destroy_fn destroy;
    char error[NDS_HCOMM_ERROR_CAPACITY];
} nds_hcomm_api;

int nds_hcomm_open(nds_hcomm_api *api, const char *library_path);
void nds_hcomm_close(nds_hcomm_api *api);
const char *nds_hcomm_error(const nds_hcomm_api *api);

#ifdef __cplusplus
}
#endif

#endif
