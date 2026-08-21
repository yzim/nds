#ifndef NDS_DEVICE_STORAGE_H
#define NDS_DEVICE_STORAGE_H

#include "nds/device_transport.h"
#include "nds/storage_protocol.hh"

#include <stdint.h>

typedef struct NdsDeviceStorageContext {
    NdsDeviceTransport transport;
    NdsDeviceSge command_buffer;
    NdsDeviceSge completion;
    uint64_t capacity;
} NdsDeviceStorageContext;

typedef struct NdsDeviceStorageReadArgs {
    NdsDeviceStorageContext context;
    nds::StorageReadCommand command;
    int32_t return_value;
    uint32_t reserved;
} NdsDeviceStorageReadArgs;

typedef struct NdsDeviceStorageWriteArgs {
    NdsDeviceStorageContext context;
    nds::StorageWriteCommand command;
    int32_t return_value;
    uint32_t reserved;
} NdsDeviceStorageWriteArgs;

typedef struct NdsDeviceStorageBatchReadArgs {
    NdsDeviceStorageContext context;
    nds::StorageBatchReadCommand command;
    int32_t return_value;
    uint32_t reserved;
} NdsDeviceStorageBatchReadArgs;

typedef struct NdsDeviceStorageBatchWriteArgs {
    NdsDeviceStorageContext context;
    nds::StorageBatchWriteCommand command;
    int32_t return_value;
    uint32_t reserved;
} NdsDeviceStorageBatchWriteArgs;

typedef struct NdsDeviceStorageWaitArgs {
    NdsDeviceStorageContext context;
    uint64_t command_id;
    uint64_t expected_bytes;
    int32_t return_value;
    uint32_t reserved;
} NdsDeviceStorageWaitArgs;

static inline int nds_device_storage_context_valid(const NdsDeviceStorageContext *context) {
    return context != nullptr && context->command_buffer.address != 0U &&
           context->command_buffer.local_key != 0U && context->command_buffer.length >= nds::kStorageCommandBytes &&
           context->completion.address != 0U && context->completion.local_key != 0U &&
           context->completion.length >= nds::kStorageCompletionBytes && context->capacity != 0U;
}

static inline int nds_device_storage_read_valid(const NdsDeviceStorageContext *context,
                                                const nds::StorageReadCommand *command) {
    return nds_device_storage_context_valid(context) && command != nullptr && command->command_id != 0U &&
           command->length != 0U && command->data.address != 0U && command->data.remote_key != 0U &&
           command->data.length >= command->length && command->offset <= context->capacity &&
           command->length <= context->capacity - command->offset;
}

static inline int nds_device_storage_write_valid(const NdsDeviceStorageContext *context,
                                                 const nds::StorageWriteCommand *command) {
    return nds_device_storage_context_valid(context) && command != nullptr && command->command_id != 0U &&
           command->length != 0U && command->data.address != 0U && command->data.remote_key != 0U &&
           command->data.length >= command->length && command->offset <= context->capacity &&
           command->length <= context->capacity - command->offset;
}

static inline int nds_device_storage_batch_read_valid(const NdsDeviceStorageContext *context,
                                                      const nds::StorageBatchReadCommand *command) {
    return nds_device_storage_context_valid(context) && command != nullptr && command->command_id != 0U &&
           command->entry_count != 0U && command->entry_count <= nds::kStorageMaxBatchEntries &&
           command->total_length != 0U && command->entries.address != 0U && command->entries.remote_key != 0U &&
           command->entries.length >= command->entry_count * nds::kStorageBatchEntryBytes;
}

static inline int nds_device_storage_batch_write_valid(const NdsDeviceStorageContext *context,
                                                       const nds::StorageBatchWriteCommand *command) {
    return nds_device_storage_context_valid(context) && command != nullptr && command->command_id != 0U &&
           command->entry_count != 0U && command->entry_count <= nds::kStorageMaxBatchEntries &&
           command->total_length != 0U && command->entries.address != 0U && command->entries.remote_key != 0U &&
           command->entries.length >= command->entry_count * nds::kStorageBatchEntryBytes;
}

static inline int nds_device_storage_wait_valid(const NdsDeviceStorageContext *context, uint64_t command_id,
                                                uint64_t expected_bytes) {
    return nds_device_storage_context_valid(context) && command_id != 0U && expected_bytes != 0U;
}

static_assert(sizeof(NdsDeviceStorageContext) == 272, "device storage context ABI changed");
static_assert(sizeof(NdsDeviceStorageReadArgs) == 328, "device storage read ABI changed");
static_assert(sizeof(NdsDeviceStorageWriteArgs) == 328, "device storage write ABI changed");
static_assert(sizeof(NdsDeviceStorageBatchReadArgs) == 328, "device storage batch-read ABI changed");
static_assert(sizeof(NdsDeviceStorageBatchWriteArgs) == 328, "device storage batch-write ABI changed");
static_assert(sizeof(NdsDeviceStorageWaitArgs) == 296, "device storage wait ABI changed");

#endif
