#ifndef NDS_DEVICE_TRANSPORT_H
#define NDS_DEVICE_TRANSPORT_H

#include "nds/device_verbs.h"

#include <stdint.h>

#define NDS_DEVICE_TRANSPORT_ABI_VERSION UINT32_C(1)

/* One peer transport. Future revisions add a data-QP view beside control_qp. */
typedef struct NdsDeviceTransport {
    uint32_t abi_version;
    uint32_t size;
    NdsDeviceQp control_qp;
} NdsDeviceTransport;

#if defined(__cplusplus)
static_assert(sizeof(NdsDeviceTransport) == 248, "device transport ABI changed");
#else
_Static_assert(sizeof(NdsDeviceTransport) == 248, "device transport ABI changed");
#endif

#endif
