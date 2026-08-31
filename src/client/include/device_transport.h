#ifndef NDS_DEVICE_TRANSPORT_H
#define NDS_DEVICE_TRANSPORT_H

#include "device_verbs.h"

#include <stdint.h>

/* Device-visible transport descriptor. The QP descriptor array belongs to the
 * host Transport; queue_index selects one QP for each launch. */
typedef struct NdsDeviceTransport {
    uint64_t qp_descriptors_address;
    uint32_t qp_count;
    uint32_t reserved;
} NdsDeviceTransport;

static inline const NdsDeviceQp *nds_device_transport_qp(const NdsDeviceTransport *transport, uint32_t queue_index) {
    if (transport == 0 || queue_index >= transport->qp_count || transport->qp_descriptors_address == 0U)
        return 0;
    return (const NdsDeviceQp *)(uintptr_t)(transport->qp_descriptors_address) + queue_index;
}

/* Legacy transport-operation envelopes select QP zero. New verbs submission
 * uses NdsDeviceQp directly after Transport selects the QueueHandle index. */
typedef struct NdsDeviceRdmaSendArgs {
    NdsDeviceTransport transport;
    NdsDeviceSendWr wr;
    int32_t return_value;
} NdsDeviceRdmaSendArgs;

typedef struct NdsDeviceRdmaRecvArgs {
    NdsDeviceTransport transport;
    NdsDeviceRecvWr wr;
    int32_t return_value;
} NdsDeviceRdmaRecvArgs;

typedef struct NdsDeviceRdmaReadArgs {
    NdsDeviceTransport transport;
    NdsDeviceSendWr wr;
    int32_t return_value;
} NdsDeviceRdmaReadArgs;

typedef struct NdsDeviceRdmaWriteArgs {
    NdsDeviceTransport transport;
    NdsDeviceSendWr wr;
    int32_t return_value;
} NdsDeviceRdmaWriteArgs;

#if defined(__cplusplus)
static_assert(sizeof(NdsDeviceTransport) == 16, "device transport ABI changed");
static_assert(sizeof(NdsDeviceRdmaSendArgs) == 72, "device RDMA send args ABI changed");
static_assert(sizeof(NdsDeviceRdmaRecvArgs) == 48, "device RDMA recv args ABI changed");
#else
_Static_assert(sizeof(NdsDeviceTransport) == 16, "device transport ABI changed");
_Static_assert(sizeof(NdsDeviceRdmaSendArgs) == 72, "device RDMA send args ABI changed");
_Static_assert(sizeof(NdsDeviceRdmaRecvArgs) == 48, "device RDMA recv args ABI changed");
#endif

#endif
