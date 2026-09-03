#ifndef NDS_BACKEND_STORAGE_H
#define NDS_BACKEND_STORAGE_H

#include "backend_transport.h"
#include "storage_protocol.hh"

#include <stdint.h>

typedef struct NdsStorageDescriptor {
    NdsTransportDescriptor transport;
    uint64_t slot_descriptors_address;
    uint64_t storage_states_address;
    uint32_t slot_count;
    uint32_t reserved;
    uint64_t capacity;
} NdsStorageDescriptor;

typedef struct NdsStorageBootstrapDescriptor {
    NdsTransportDescriptor transport;
    NdsSge bootstrap;
} NdsStorageBootstrapDescriptor;

typedef struct NdsStorageSlotDescriptor {
    NdsSge command_buffer;
    NdsSge completion_buffer;
    uint32_t qp_index;
    uint32_t reserved;
} NdsStorageSlotDescriptor;

/* The backend claims this state before posting the command. The host control
 * path assigns command_id when it reserves a slot and clears the record when
 * the caller releases the completed slot. */
typedef struct NdsStorageState {
    uint64_t command_id;
    uint32_t expected_bytes;
    uint32_t in_flight;
    uint32_t reserved;
} NdsStorageState;

typedef struct NdsStorageOperation {
    uint32_t slot_id;
    uint32_t reserved;
    uint64_t server_offset;
    uint64_t buffer_address;
    uint32_t buffer_key;
    uint32_t length;
} NdsStorageOperation;

typedef struct NdsStorageBatchOperation {
    uint32_t slot_id;
    uint32_t entry_count;
    uint64_t entries_address;
    uint32_t entries_key;
    uint32_t reserved;
} NdsStorageBatchOperation;

typedef struct NdsStorageBootstrapArgs {
    NdsStorageBootstrapDescriptor bootstrap;
    int32_t return_value;
} NdsStorageBootstrapArgs;

typedef struct NdsStorageOperationArgs {
    NdsStorageDescriptor storage;
    NdsStorageOperation operation;
    int32_t return_value;
} NdsStorageOperationArgs;

typedef struct NdsStorageBatchOperationArgs {
    NdsStorageDescriptor storage;
    NdsStorageBatchOperation operation;
    int32_t return_value;
} NdsStorageBatchOperationArgs;

typedef struct NdsStorageWaitArgs {
    NdsStorageDescriptor storage;
    uint32_t slot_id;
    uint32_t reserved;
    int32_t return_value;
} NdsStorageWaitArgs;

static inline uint32_t nds_storage_slot_id(uint32_t queue_index, uint32_t slot_index) {
    return (queue_index << 16U) | (slot_index & UINT32_C(0xffff));
}

static inline uint32_t nds_storage_slot_id_queue(uint32_t slot_id) {
    return slot_id >> 16U;
}

static inline uint32_t nds_storage_slot_id_index(uint32_t slot_id) {
    return slot_id & UINT32_C(0xffff);
}

static inline int nds_storage_descriptor_valid(const NdsStorageDescriptor *descriptor) {
    return descriptor != nullptr && descriptor->slot_descriptors_address != 0U &&
           descriptor->storage_states_address != 0U && descriptor->slot_count != 0U &&
           descriptor->transport.qp_count != 0U && descriptor->capacity != 0U && descriptor->reserved == 0U &&
           descriptor->transport.reserved == 0U && descriptor->transport.qp_descriptors_address != 0U &&
           descriptor->transport.qp_states_address != 0U;
}

static inline NdsStorageState *nds_storage_state(const NdsStorageDescriptor *descriptor, uint32_t slot_index) {
    if (!nds_storage_descriptor_valid(descriptor) || slot_index >= descriptor->slot_count)
        return nullptr;
    return (NdsStorageState *)(uintptr_t)(descriptor->storage_states_address) + slot_index;
}

static inline const NdsStorageSlotDescriptor *nds_storage_slot(const NdsStorageDescriptor *descriptor,
                                                               uint32_t slot_index) {
    if (!nds_storage_descriptor_valid(descriptor) || slot_index >= descriptor->slot_count)
        return nullptr;
    return (const NdsStorageSlotDescriptor *)(uintptr_t)(descriptor->slot_descriptors_address) + slot_index;
}

static inline int nds_storage_slot_valid(const NdsStorageDescriptor *descriptor, uint32_t slot_index) {
    const NdsStorageSlotDescriptor *slot = nds_storage_slot(descriptor, slot_index);
    return slot != nullptr && slot->reserved == 0U && slot->qp_index < descriptor->transport.qp_count &&
           slot->command_buffer.address != 0U && slot->command_buffer.local_key != 0U &&
           slot->command_buffer.length >= nds::kStorageCommandBytes && slot->completion_buffer.address != 0U &&
           slot->completion_buffer.local_key != 0U && slot->completion_buffer.length >= nds::kStorageCompletionBytes;
}

static inline int nds_storage_slot_id_valid(const NdsStorageDescriptor *descriptor, uint32_t slot_id) {
    const uint32_t slot_index = nds_storage_slot_id_index(slot_id);
    const uint32_t queue_index = nds_storage_slot_id_queue(slot_id);
    const NdsStorageSlotDescriptor *slot = nds_storage_slot(descriptor, slot_index);
    return nds_storage_slot_valid(descriptor, slot_index) && slot->qp_index == queue_index;
}

#if defined(__CCE_AICORE__)
__aicore__ __gm__ inline const NdsStorageSlotDescriptor *nds_storage_slot_global(
    __gm__ const NdsStorageDescriptor *descriptor, uint32_t slot_index) {
    if (descriptor == 0 || descriptor->slot_descriptors_address == 0U || slot_index >= descriptor->slot_count)
        return 0;
    return (__gm__ const NdsStorageSlotDescriptor *)(uintptr_t)(descriptor->slot_descriptors_address) + slot_index;
}

__aicore__ __gm__ inline NdsStorageState *nds_storage_state_global(__gm__ const NdsStorageDescriptor *descriptor,
                                                                   uint32_t slot_index) {
    if (descriptor == 0 || descriptor->storage_states_address == 0U || slot_index >= descriptor->slot_count)
        return 0;
    return (__gm__ NdsStorageState *)(uintptr_t)(descriptor->storage_states_address) + slot_index;
}
#endif

static inline int nds_storage_read_valid(const NdsStorageDescriptor *descriptor,
                                         const nds::StorageReadCommand *command) {
    return nds_storage_slot_valid(descriptor, command == nullptr ? 0U : command->slot_index) && command != nullptr &&
           command->command_id != 0U && command->length != 0U && command->data.address != 0U &&
           command->data.remote_key != 0U && command->data.length >= command->length &&
           command->offset <= descriptor->capacity && command->length <= descriptor->capacity - command->offset &&
           command->length <= UINT32_MAX;
}

static inline int nds_storage_write_valid(const NdsStorageDescriptor *descriptor,
                                          const nds::StorageWriteCommand *command) {
    return nds_storage_slot_valid(descriptor, command == nullptr ? 0U : command->slot_index) && command != nullptr &&
           command->command_id != 0U && command->length != 0U && command->data.address != 0U &&
           command->data.remote_key != 0U && command->data.length >= command->length &&
           command->offset <= descriptor->capacity && command->length <= descriptor->capacity - command->offset &&
           command->length <= UINT32_MAX;
}

static inline int nds_storage_batch_read_valid(const NdsStorageDescriptor *descriptor,
                                               const nds::StorageBatchReadCommand *command) {
    return nds_storage_slot_valid(descriptor, command == nullptr ? 0U : command->slot_index) && command != nullptr &&
           command->command_id != 0U && command->entry_count != 0U &&
           command->entry_count <= nds::kStorageMaxBatchEntries && command->total_length != 0U &&
           command->entries.address != 0U && command->entries.remote_key != 0U &&
           command->entries.length >= command->entry_count * nds::kStorageBatchEntryBytes;
}

static inline int nds_storage_batch_write_valid(const NdsStorageDescriptor *descriptor,
                                                const nds::StorageBatchWriteCommand *command) {
    return nds_storage_slot_valid(descriptor, command == nullptr ? 0U : command->slot_index) && command != nullptr &&
           command->command_id != 0U && command->entry_count != 0U &&
           command->entry_count <= nds::kStorageMaxBatchEntries && command->total_length != 0U &&
           command->entries.address != 0U && command->entries.remote_key != 0U &&
           command->entries.length >= command->entry_count * nds::kStorageBatchEntryBytes;
}

static inline int nds_storage_wait_valid(const NdsStorageDescriptor *descriptor, uint32_t slot_id) {
    return nds_storage_slot_id_valid(descriptor, slot_id);
}

static_assert(sizeof(NdsStorageSlotDescriptor) == 40, "device storage slot ABI changed");
static_assert(sizeof(NdsStorageState) == 24, "device storage state ABI changed");
static_assert(sizeof(NdsStorageOperation) == 32, "device storage operation ABI changed");
static_assert(sizeof(NdsStorageBatchOperation) == 24, "device storage batch operation ABI changed");
static_assert(sizeof(NdsStorageDescriptor) == 56, "device storage descriptor ABI changed");
static_assert(sizeof(NdsStorageBootstrapDescriptor) == 40, "device storage bootstrap ABI changed");
static_assert(sizeof(NdsStorageBootstrapArgs) == 48, "device storage bootstrap args ABI changed");
static_assert(sizeof(NdsStorageOperationArgs) == 96, "device storage operation ABI changed");
static_assert(sizeof(NdsStorageBatchOperationArgs) == 88, "device storage batch operation ABI changed");
static_assert(sizeof(NdsStorageWaitArgs) == 72, "device storage wait ABI changed");

#endif
