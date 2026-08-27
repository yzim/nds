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
    uint64_t return_value_address;
} NdsDeviceRdmaSendArgs;

typedef struct NdsDeviceRdmaRecvArgs {
    NdsDeviceTransport transport;
    NdsDeviceRecvWr wr;
    uint64_t return_value_address;
} NdsDeviceRdmaRecvArgs;

typedef struct NdsDeviceRdmaReadArgs {
    NdsDeviceTransport transport;
    NdsDeviceSendWr wr;
    uint64_t return_value_address;
} NdsDeviceRdmaReadArgs;

typedef struct NdsDeviceRdmaWriteArgs {
    NdsDeviceTransport transport;
    NdsDeviceSendWr wr;
    uint64_t return_value_address;
} NdsDeviceRdmaWriteArgs;

#if defined(__cplusplus)
static_assert(sizeof(NdsDeviceTransport) == 232, "device transport ABI changed");
static_assert(sizeof(NdsDeviceRdmaSendArgs) == 288, "device RDMA send args ABI changed");
static_assert(sizeof(NdsDeviceRdmaRecvArgs) == 264, "device RDMA recv args ABI changed");
static_assert(sizeof(NdsDeviceRdmaReadArgs) == 288, "device RDMA read args ABI changed");
static_assert(sizeof(NdsDeviceRdmaWriteArgs) == 288, "device RDMA write args ABI changed");
static_assert(offsetof(NdsDeviceRdmaSendArgs, return_value_address) + sizeof(uint64_t) == sizeof(NdsDeviceRdmaSendArgs),
              "device RDMA send result must be final");
static_assert(offsetof(NdsDeviceRdmaRecvArgs, return_value_address) + sizeof(uint64_t) == sizeof(NdsDeviceRdmaRecvArgs),
              "device RDMA recv result must be final");
static_assert(offsetof(NdsDeviceRdmaReadArgs, return_value_address) + sizeof(uint64_t) == sizeof(NdsDeviceRdmaReadArgs),
              "device RDMA read result must be final");
static_assert(offsetof(NdsDeviceRdmaWriteArgs, return_value_address) + sizeof(uint64_t) ==
                  sizeof(NdsDeviceRdmaWriteArgs),
              "device RDMA write result must be final");
#else
_Static_assert(sizeof(NdsDeviceTransport) == 232, "device transport ABI changed");
_Static_assert(sizeof(NdsDeviceRdmaSendArgs) == 288, "device RDMA send args ABI changed");
_Static_assert(sizeof(NdsDeviceRdmaRecvArgs) == 264, "device RDMA recv args ABI changed");
_Static_assert(sizeof(NdsDeviceRdmaReadArgs) == 288, "device RDMA read args ABI changed");
_Static_assert(sizeof(NdsDeviceRdmaWriteArgs) == 288, "device RDMA write args ABI changed");
_Static_assert(offsetof(NdsDeviceRdmaSendArgs, return_value_address) + sizeof(uint64_t) ==
                   sizeof(NdsDeviceRdmaSendArgs),
               "device RDMA send result must be final");
_Static_assert(offsetof(NdsDeviceRdmaRecvArgs, return_value_address) + sizeof(uint64_t) ==
                   sizeof(NdsDeviceRdmaRecvArgs),
               "device RDMA recv result must be final");
_Static_assert(offsetof(NdsDeviceRdmaReadArgs, return_value_address) + sizeof(uint64_t) ==
                   sizeof(NdsDeviceRdmaReadArgs),
               "device RDMA read result must be final");
_Static_assert(offsetof(NdsDeviceRdmaWriteArgs, return_value_address) + sizeof(uint64_t) ==
                   sizeof(NdsDeviceRdmaWriteArgs),
               "device RDMA write result must be final");
#endif

#endif
