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

typedef struct NdsDeviceTransfer {
    uint64_t wr_id;
    NdsDeviceSge local;
    uint64_t remote_address;
    uint32_t remote_key;
    uint32_t reserved;
} NdsDeviceTransfer;

static inline void nds_device_build_send_wr(const NdsDeviceTransfer *transfer, uint32_t opcode,
                                            NdsDeviceSendWr *wr) {
    wr->wr_id = transfer->wr_id;
    wr->opcode = opcode;
    wr->flags = NDS_DEVICE_SEND_SIGNALED;
    wr->local = transfer->local;
    wr->remote_address = transfer->remote_address;
    wr->remote_key = transfer->remote_key;
    wr->reserved = 0U;
}

static inline void nds_device_build_recv_wr(const NdsDeviceTransfer *transfer, NdsDeviceRecvWr *wr) {
    wr->wr_id = transfer->wr_id;
    wr->local = transfer->local;
}

#if defined(__cplusplus)
static_assert(sizeof(NdsDeviceTransport) == 248, "device transport ABI changed");
static_assert(sizeof(NdsDeviceTransfer) == 40, "device transfer ABI changed");
#else
_Static_assert(sizeof(NdsDeviceTransport) == 248, "device transport ABI changed");
_Static_assert(sizeof(NdsDeviceTransfer) == 40, "device transfer ABI changed");
#endif

#endif
