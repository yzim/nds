#ifndef NDS_ACL_LOADER_H
#define NDS_ACL_LOADER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Minimal AscendCL lifecycle adapter.  The default build resolves this public
 * ABI from the selected CANN installation at runtime.  A version-pinned build
 * may instead link the official ACL headers/library behind this same adapter
 * (NDS_LINK_PUBLIC_ACL), without changing the consumer-facing API.
 */
typedef int (*nds_acl_init_fn)(const char *config_path);
typedef int (*nds_acl_finalize_fn)(void);
typedef int (*nds_acl_rt_set_device_fn)(int32_t device_id);

enum { NDS_ACL_ERROR_CAPACITY = 512 };

typedef struct nds_acl_api {
    void *library;
    nds_acl_init_fn init;
    nds_acl_finalize_fn finalize;
    nds_acl_rt_set_device_fn set_device;
    char error[NDS_ACL_ERROR_CAPACITY];
} nds_acl_api;

int nds_acl_open(nds_acl_api *api, const char *library_path);
void nds_acl_close(nds_acl_api *api);
const char *nds_acl_error(const nds_acl_api *api);

#ifdef __cplusplus
}
#endif

#endif
