#ifndef NDS_BACKEND_STORAGE_H
#define NDS_BACKEND_STORAGE_H

#include "backend_transport.h"
#include "storage_protocol.hh"

#include <stdint.h>

typedef struct NdsStorageDescriptor {
    NdsTransportDescriptor transport;
    uint64_t slot_descriptors_address;
    uint32_t slot_count;
    uint32_t reserved;
    uint64_t capacity;
} NdsStorageDescriptor;

typedef struct NdsStorageSlotDescriptor {
    NdsSge command_buffer;
    NdsSge completion_buffer;
    uint32_t qp_index;
    uint32_t reserved;
} NdsStorageSlotDescriptor;

typedef struct NdsStorageReadArgs {
    NdsStorageDescriptor storage;
    nds::StorageReadCommand command;
    int32_t return_value;
} NdsStorageReadArgs;

typedef struct NdsStorageWriteArgs {
    NdsStorageDescriptor storage;
    nds::StorageWriteCommand command;
    int32_t return_value;
} NdsStorageWriteArgs;

typedef struct NdsStorageBatchReadArgs {
    NdsStorageDescriptor storage;
    nds::StorageBatchReadCommand command;
    int32_t return_value;
} NdsStorageBatchReadArgs;

typedef struct NdsStorageBatchWriteArgs {
    NdsStorageDescriptor storage;
    nds::StorageBatchWriteCommand command;
    int32_t return_value;
} NdsStorageBatchWriteArgs;

typedef struct NdsStorageWaitArgs {
    NdsStorageDescriptor storage;
    uint64_t command_id;
    uint64_t expected_bytes;
    uint32_t slot_index;
    uint32_t reserved;
    int32_t return_value;
} NdsStorageWaitArgs;

static inline int nds_storage_descriptor_valid(const NdsStorageDescriptor *descriptor) {
    return descriptor != nullptr && descriptor->slot_descriptors_address != 0U && descriptor->slot_count != 0U &&
           descriptor->transport.qp_count != 0U && descriptor->capacity != 0U && descriptor->reserved == 0U &&
           descriptor->transport.reserved == 0U && descriptor->transport.qp_descriptors_address != 0U &&
           descriptor->transport.qp_states_address != 0U;
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

#if defined(__CCE_AICORE__)
__aicore__ __gm__ inline const NdsStorageSlotDescriptor *nds_storage_slot_global(
    __gm__ const NdsStorageDescriptor *descriptor, uint32_t slot_index) {
    if (descriptor == 0 || descriptor->slot_descriptors_address == 0U || slot_index >= descriptor->slot_count)
        return 0;
    return (__gm__ const NdsStorageSlotDescriptor *)(uintptr_t)(descriptor->slot_descriptors_address) + slot_index;
}
#endif

static inline int nds_storage_read_valid(const NdsStorageDescriptor *descriptor,
                                         const nds::StorageReadCommand *command) {
    return nds_storage_slot_valid(descriptor, command == nullptr ? 0U : command->slot_index) && command != nullptr &&
           command->command_id != 0U && command->length != 0U && command->data.address != 0U &&
           command->data.remote_key != 0U && command->data.length >= command->length &&
           command->offset <= descriptor->capacity && command->length <= descriptor->capacity - command->offset;
}

static inline int nds_storage_write_valid(const NdsStorageDescriptor *descriptor,
                                          const nds::StorageWriteCommand *command) {
    return nds_storage_slot_valid(descriptor, command == nullptr ? 0U : command->slot_index) && command != nullptr &&
           command->command_id != 0U && command->length != 0U && command->data.address != 0U &&
           command->data.remote_key != 0U && command->data.length >= command->length &&
           command->offset <= descriptor->capacity && command->length <= descriptor->capacity - command->offset;
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

static inline int nds_storage_wait_valid(const NdsStorageDescriptor *descriptor, uint64_t command_id,
                                         uint64_t expected_bytes, uint32_t slot_index) {
    return nds_storage_slot_valid(descriptor, slot_index) && command_id != 0U && expected_bytes != 0U;
}

static_assert(sizeof(NdsStorageSlotDescriptor) == 40, "device storage slot ABI changed");
static_assert(sizeof(NdsStorageDescriptor) == 48, "device storage descriptor ABI changed");
static_assert(sizeof(NdsStorageReadArgs) == 112, "device storage read ABI changed");
static_assert(sizeof(NdsStorageWriteArgs) == 112, "device storage write ABI changed");
static_assert(sizeof(NdsStorageBatchReadArgs) == 112, "device storage batch-read ABI changed");
static_assert(sizeof(NdsStorageBatchWriteArgs) == 112, "device storage batch-write ABI changed");
static_assert(sizeof(NdsStorageWaitArgs) == 80, "device storage wait ABI changed");
static_assert(offsetof(NdsStorageReadArgs, return_value) > offsetof(NdsStorageReadArgs, command),
              "device storage read result must follow the request");
static_assert(offsetof(NdsStorageWriteArgs, return_value) > offsetof(NdsStorageWriteArgs, command),
              "device storage write result must follow the request");
static_assert(offsetof(NdsStorageBatchReadArgs, return_value) > offsetof(NdsStorageBatchReadArgs, command),
              "device storage batch-read result must follow the request");
static_assert(offsetof(NdsStorageBatchWriteArgs, return_value) > offsetof(NdsStorageBatchWriteArgs, command),
              "device storage batch-write result must follow the request");
static_assert(offsetof(NdsStorageWaitArgs, return_value) > offsetof(NdsStorageWaitArgs, expected_bytes),
              "device storage wait result must follow the request");

#endif
