#ifndef NDS_DEVICE_TRANSPORT_H
#define NDS_DEVICE_TRANSPORT_H

#include "nds/device_verbs.h"

#include <stdint.h>

/* One peer transport. Future revisions add a data-QP view beside control_qp. */
typedef struct NdsDeviceTransport {
    NdsDeviceQp control_qp;
} NdsDeviceTransport;

/* Dedicated transport-layer request envelopes. Each carries the transport and
 * the per-operation work-request payload. */
typedef struct NdsDeviceRdmaSendArgs {
    NdsDeviceTransport transport;
    NdsDeviceSendWr wr;
    int32_t return_value;
    uint32_t reserved;
} NdsDeviceRdmaSendArgs;

typedef struct NdsDeviceRdmaRecvArgs {
    NdsDeviceTransport transport;
    NdsDeviceRecvWr wr;
    int32_t return_value;
    uint32_t reserved;
} NdsDeviceRdmaRecvArgs;

typedef struct NdsDeviceRdmaReadArgs {
    NdsDeviceTransport transport;
    NdsDeviceSendWr wr;
    int32_t return_value;
    uint32_t reserved;
} NdsDeviceRdmaReadArgs;

typedef struct NdsDeviceRdmaWriteArgs {
    NdsDeviceTransport transport;
    NdsDeviceSendWr wr;
    int32_t return_value;
    uint32_t reserved;
} NdsDeviceRdmaWriteArgs;

#if defined(__cplusplus)
static_assert(sizeof(NdsDeviceTransport) == 232, "device transport ABI changed");
static_assert(sizeof(NdsDeviceRdmaSendArgs) == 288, "device RDMA send args ABI changed");
static_assert(sizeof(NdsDeviceRdmaRecvArgs) == 264, "device RDMA recv args ABI changed");
static_assert(sizeof(NdsDeviceRdmaReadArgs) == 288, "device RDMA read args ABI changed");
static_assert(sizeof(NdsDeviceRdmaWriteArgs) == 288, "device RDMA write args ABI changed");
#else
_Static_assert(sizeof(NdsDeviceTransport) == 232, "device transport ABI changed");
_Static_assert(sizeof(NdsDeviceRdmaSendArgs) == 288, "device RDMA send args ABI changed");
_Static_assert(sizeof(NdsDeviceRdmaRecvArgs) == 264, "device RDMA recv args ABI changed");
_Static_assert(sizeof(NdsDeviceRdmaReadArgs) == 288, "device RDMA read args ABI changed");
_Static_assert(sizeof(NdsDeviceRdmaWriteArgs) == 288, "device RDMA write args ABI changed");
#endif

#endif
