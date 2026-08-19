#ifndef NDS_DEVICE_STORAGE_H
#define NDS_DEVICE_STORAGE_H

#include "nds/device_transport.h"
#include "nds/protocol.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define NDS_DEVICE_STORAGE_ABI_VERSION UINT32_C(1)

typedef struct nds_device_storage {
    uint32_t abi_version;
    uint32_t size;
    nds_device_transport transport;
    nds_device_sge command;
    nds_device_sge completion;
    uint64_t capacity;
    uint64_t request_id;
} nds_device_storage;

typedef struct nds_device_storage_io {
    uint16_t operation;
    uint16_t reserved;
    uint32_t length;
    uint64_t offset;
    nds_device_sge data;
    uint32_t data_rkey;
    uint32_t reserved1;
} nds_device_storage_io;

typedef struct nds_device_storage_request {
    uint32_t abi_version;
    uint32_t size;
    nds_device_storage storage;
    nds_device_storage_io io;
    uint64_t operation_result_address;
} nds_device_storage_request;

static inline uint16_t nds_device_htons(uint16_t value) {
    return (uint16_t)((value << 8) | (value >> 8));
}

static inline uint32_t nds_device_htonl(uint32_t value) {
    return (value << 24) | ((value << 8) & 0x00ff0000U) | ((value >> 8) & 0x0000ff00U) | (value >> 24);
}

static inline uint64_t nds_device_htonll(uint64_t value) {
    return ((uint64_t)nds_device_htonl((uint32_t)value) << 32) | nds_device_htonl((uint32_t)(value >> 32));
}

static inline int nds_device_storage_valid(const nds_device_storage *storage, const nds_device_storage_io *io) {
    uint32_t required_access;
    if (storage == NULL || io == NULL || storage->abi_version != NDS_DEVICE_STORAGE_ABI_VERSION ||
        storage->size != sizeof(*storage) || storage->transport.abi_version != NDS_DEVICE_TRANSPORT_ABI_VERSION ||
        storage->transport.size != sizeof(storage->transport) || storage->request_id == 0U ||
        storage->command.address == 0U || storage->command.local_key == 0U ||
        storage->command.length < sizeof(nds_protocol_command_wire) || storage->completion.address == 0U ||
        storage->completion.local_key == 0U || storage->completion.length < sizeof(nds_protocol_completion_wire) ||
        io->length == 0U || io->data.address == 0U || io->data.local_key == 0U || io->data_rkey == 0U ||
        io->data.length < io->length || (io->operation != NDS_PROTOCOL_READ && io->operation != NDS_PROTOCOL_WRITE)) {
        return 0;
    }
    if (io->offset > storage->capacity || io->length > storage->capacity - io->offset)
        return 0;
    required_access =
        io->operation == NDS_PROTOCOL_READ ? NDS_PROTOCOL_ACCESS_REMOTE_WRITE : NDS_PROTOCOL_ACCESS_REMOTE_READ;
    (void)required_access;
    return 1;
}

static inline void nds_device_storage_encode_command(const nds_device_storage *storage, const nds_device_storage_io *io,
                                                     nds_protocol_command_wire *wire) {
    const uint32_t access =
        io->operation == NDS_PROTOCOL_READ ? NDS_PROTOCOL_ACCESS_REMOTE_WRITE : NDS_PROTOCOL_ACCESS_REMOTE_READ;
    (void)memset(wire, 0, sizeof(*wire));
    wire->magic = nds_device_htonl(NDS_PROTOCOL_COMMAND_MAGIC);
    wire->version = nds_device_htons(NDS_PROTOCOL_VERSION);
    wire->operation = nds_device_htons(io->operation);
    wire->request_id = nds_device_htonll(storage->request_id);
    wire->offset = nds_device_htonll(io->offset);
    wire->length = nds_device_htonll(io->length);
    wire->data_address = nds_device_htonll(io->data.address);
    wire->data_length = nds_device_htonll(io->data.length);
    wire->data_rkey = nds_device_htonl(io->data_rkey);
    wire->data_access = nds_device_htonl(access);
}

static inline void nds_device_storage_encode_pending(const nds_device_storage *storage,
                                                     nds_protocol_completion_wire *wire) {
    (void)memset(wire, 0, sizeof(*wire));
    wire->magic = nds_device_htonl(NDS_PROTOCOL_COMPLETION_MAGIC);
    wire->version = nds_device_htons(NDS_PROTOCOL_VERSION);
    wire->state = nds_device_htons(NDS_PROTOCOL_COMPLETION_PENDING);
    wire->status = nds_device_htons(NDS_PROTOCOL_SUCCESS);
    wire->request_id = nds_device_htonll(storage->request_id);
    wire->bytes_transferred = 0U;
}

static inline int nds_device_storage_completion_done(const nds_protocol_completion_wire *wire, uint64_t request_id,
                                                     uint64_t expected_bytes) {
    if (nds_device_htonl(wire->magic) != NDS_PROTOCOL_COMPLETION_MAGIC ||
        nds_device_htons(wire->version) != NDS_PROTOCOL_VERSION ||
        nds_device_htons(wire->state) != NDS_PROTOCOL_COMPLETION_COMPLETE ||
        nds_device_htonll(wire->request_id) != request_id || nds_device_htons(wire->status) != NDS_PROTOCOL_SUCCESS ||
        nds_device_htonll(wire->bytes_transferred) != expected_bytes) {
        return 0;
    }
    return 1;
}

#if defined(__cplusplus)
static_assert(sizeof(nds_device_storage) == 304, "device storage ABI changed");
static_assert(sizeof(nds_device_storage_io) == 40, "device storage IO ABI changed");
static_assert(sizeof(nds_device_storage_request) == 360, "device storage request ABI changed");
#else
_Static_assert(sizeof(nds_device_storage) == 304, "device storage ABI changed");
_Static_assert(sizeof(nds_device_storage_io) == 40, "device storage IO ABI changed");
_Static_assert(sizeof(nds_device_storage_request) == 360, "device storage request ABI changed");
#endif

#endif
