#ifndef NDS_CLIENT_AIV_HOST_LAUNCHER_HH
#define NDS_CLIENT_AIV_HOST_LAUNCHER_HH

#include "device_storage.h"
#include "device_transport.h"
#include "device_verbs.h"

#include <acl/acl_rt.h>

#ifdef __cplusplus
extern "C" {
#endif

/* These wrappers are the host-facing AIV ABI. The request records remain
 * device-visible and are shared with the AICPU and RA implementations. */
int nds_aiv_host_post_send(aclrtStream stream, NdsDevicePostSendArgs *request);
int nds_aiv_host_post_recv(aclrtStream stream, NdsDevicePostRecvArgs *request);
int nds_aiv_host_poll_cq(aclrtStream stream, NdsDevicePollCqArgs *request);
int nds_aiv_host_rdma_send(aclrtStream stream, NdsDeviceRdmaSendArgs *request);
int nds_aiv_host_rdma_recv(aclrtStream stream, NdsDeviceRdmaRecvArgs *request);
int nds_aiv_host_rdma_read(aclrtStream stream, NdsDeviceRdmaReadArgs *request);
int nds_aiv_host_rdma_write(aclrtStream stream, NdsDeviceRdmaWriteArgs *request);
int nds_aiv_host_storage_read(aclrtStream stream, NdsDeviceStorageReadArgs *request);
int nds_aiv_host_storage_write(aclrtStream stream, NdsDeviceStorageWriteArgs *request);
int nds_aiv_host_storage_batch_read(aclrtStream stream, NdsDeviceStorageBatchReadArgs *request);
int nds_aiv_host_storage_batch_write(aclrtStream stream, NdsDeviceStorageBatchWriteArgs *request);
int nds_aiv_host_storage_wait(aclrtStream stream, NdsDeviceStorageWaitArgs *request);

#ifdef __cplusplus
}
#endif

#endif
