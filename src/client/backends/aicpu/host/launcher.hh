#ifndef NDS_CLIENT_AICPU_HOST_LAUNCHER_HH
#define NDS_CLIENT_AICPU_HOST_LAUNCHER_HH

#include "device_storage.h"
#include "device_transport.h"
#include "device_verbs.h"

#include <acl/acl_rt.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Host facade for the standard AICPU package. The package's void *args entry
 * ABI is deliberately hidden behind the same typed request records used by
 * the AIV and RA host facades. */
int nds_aicpu_host_post_send(aclrtFuncHandle function, aclrtStream stream, NdsDevicePostSendArgs *request);
int nds_aicpu_host_post_recv(aclrtFuncHandle function, aclrtStream stream, NdsDevicePostRecvArgs *request);
int nds_aicpu_host_poll_cq(aclrtFuncHandle function, aclrtStream stream, NdsDevicePollCqArgs *request);
int nds_aicpu_host_rdma_send(aclrtFuncHandle function, aclrtStream stream, NdsDeviceRdmaSendArgs *request);
int nds_aicpu_host_rdma_recv(aclrtFuncHandle function, aclrtStream stream, NdsDeviceRdmaRecvArgs *request);
int nds_aicpu_host_rdma_read(aclrtFuncHandle function, aclrtStream stream, NdsDeviceRdmaReadArgs *request);
int nds_aicpu_host_rdma_write(aclrtFuncHandle function, aclrtStream stream, NdsDeviceRdmaWriteArgs *request);
int nds_aicpu_host_storage_read(aclrtFuncHandle function, aclrtStream stream, NdsDeviceStorageReadArgs *request);
int nds_aicpu_host_storage_write(aclrtFuncHandle function, aclrtStream stream, NdsDeviceStorageWriteArgs *request);
int nds_aicpu_host_storage_batch_read(aclrtFuncHandle function, aclrtStream stream,
                                      NdsDeviceStorageBatchReadArgs *request);
int nds_aicpu_host_storage_batch_write(aclrtFuncHandle function, aclrtStream stream,
                                       NdsDeviceStorageBatchWriteArgs *request);
int nds_aicpu_host_storage_wait(aclrtFuncHandle function, aclrtStream stream, NdsDeviceStorageWaitArgs *request);

#ifdef __cplusplus
}
#endif

#endif
