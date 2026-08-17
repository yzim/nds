#ifndef NDS_DEVICE_CONNECTION_H
#define NDS_DEVICE_CONNECTION_H

#include "nds/device_verbs.h"

#include <stdint.h>

#define NDS_DEVICE_CONNECTION_ABI_VERSION UINT32_C(1)

typedef struct nds_device_connection {
    uint32_t abi_version;
    uint32_t size;
    nds_device_qp qp;
} nds_device_connection;

typedef struct nds_device_transfer {
    uint64_t wr_id;
    nds_device_sge local;
    uint64_t remote_address;
    uint32_t remote_key;
    uint32_t reserved;
} nds_device_transfer;

static inline void nds_device_build_send_wr(const nds_device_transfer *transfer,
                                            uint32_t opcode,
                                            nds_device_send_wr *wr) {
    wr->wr_id = transfer->wr_id;
    wr->opcode = opcode;
    wr->flags = NDS_DEVICE_SEND_SIGNALED;
    wr->local = transfer->local;
    wr->remote_address = transfer->remote_address;
    wr->remote_key = transfer->remote_key;
    wr->reserved = 0U;
}

static inline void nds_device_build_recv_wr(const nds_device_transfer *transfer,
                                            nds_device_recv_wr *wr) {
    wr->wr_id = transfer->wr_id;
    wr->local = transfer->local;
}

#if defined(__cplusplus)
static_assert(sizeof(nds_device_connection) == 248, "device connection ABI changed");
static_assert(sizeof(nds_device_transfer) == 40, "device transfer ABI changed");
#else
_Static_assert(sizeof(nds_device_connection) == 248, "device connection ABI changed");
_Static_assert(sizeof(nds_device_transfer) == 40, "device transfer ABI changed");
#endif

#endif
