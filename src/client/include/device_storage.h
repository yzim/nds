#ifndef NDS_DEVICE_STORAGE_H
#define NDS_DEVICE_STORAGE_H

#include "device_transport.h"
#include "storage_protocol.hh"

#include <stdint.h>

typedef struct NdsStorageContext {
    NdsTransportDescriptor transport;
    NdsSge command_buffer;
    NdsSge completion;
    uint64_t capacity;
} NdsStorageContext;

typedef struct NdsStorageReadArgs {
    NdsStorageContext context;
    nds::StorageReadCommand command;
    int32_t return_value;
} NdsStorageReadArgs;

typedef struct NdsStorageWriteArgs {
    NdsStorageContext context;
    nds::StorageWriteCommand command;
    int32_t return_value;
} NdsStorageWriteArgs;

typedef struct NdsStorageBatchReadArgs {
    NdsStorageContext context;
    nds::StorageBatchReadCommand command;
    int32_t return_value;
} NdsStorageBatchReadArgs;

typedef struct NdsStorageBatchWriteArgs {
    NdsStorageContext context;
    nds::StorageBatchWriteCommand command;
    int32_t return_value;
} NdsStorageBatchWriteArgs;

typedef struct NdsStorageWaitArgs {
    NdsStorageContext context;
    uint64_t command_id;
    uint64_t expected_bytes;
    int32_t return_value;
} NdsStorageWaitArgs;

static inline int nds_storage_context_valid(const NdsStorageContext *context) {
    return context != nullptr && context->command_buffer.address != 0U && context->command_buffer.local_key != 0U &&
           context->command_buffer.length >= nds::kStorageCommandBytes && context->completion.address != 0U &&
           context->completion.local_key != 0U && context->completion.length >= nds::kStorageCompletionBytes &&
           context->capacity != 0U;
}

static inline int nds_storage_read_valid(const NdsStorageContext *context, const nds::StorageReadCommand *command) {
    return nds_storage_context_valid(context) && command != nullptr && command->command_id != 0U &&
           command->length != 0U && command->data.address != 0U && command->data.remote_key != 0U &&
           command->data.length >= command->length && command->offset <= context->capacity &&
           command->length <= context->capacity - command->offset;
}

static inline int nds_storage_write_valid(const NdsStorageContext *context, const nds::StorageWriteCommand *command) {
    return nds_storage_context_valid(context) && command != nullptr && command->command_id != 0U &&
           command->length != 0U && command->data.address != 0U && command->data.remote_key != 0U &&
           command->data.length >= command->length && command->offset <= context->capacity &&
           command->length <= context->capacity - command->offset;
}

static inline int nds_storage_batch_read_valid(const NdsStorageContext *context,
                                               const nds::StorageBatchReadCommand *command) {
    return nds_storage_context_valid(context) && command != nullptr && command->command_id != 0U &&
           command->entry_count != 0U && command->entry_count <= nds::kStorageMaxBatchEntries &&
           command->total_length != 0U && command->entries.address != 0U && command->entries.remote_key != 0U &&
           command->entries.length >= command->entry_count * nds::kStorageBatchEntryBytes;
}

static inline int nds_storage_batch_write_valid(const NdsStorageContext *context,
                                                const nds::StorageBatchWriteCommand *command) {
    return nds_storage_context_valid(context) && command != nullptr && command->command_id != 0U &&
           command->entry_count != 0U && command->entry_count <= nds::kStorageMaxBatchEntries &&
           command->total_length != 0U && command->entries.address != 0U && command->entries.remote_key != 0U &&
           command->entries.length >= command->entry_count * nds::kStorageBatchEntryBytes;
}

static inline int nds_storage_wait_valid(const NdsStorageContext *context, uint64_t command_id,
                                         uint64_t expected_bytes) {
    return nds_storage_context_valid(context) && command_id != 0U && expected_bytes != 0U;
}

static_assert(sizeof(NdsStorageContext) == 64, "device storage context ABI changed");
static_assert(sizeof(NdsStorageReadArgs) == 120, "device storage read ABI changed");
static_assert(sizeof(NdsStorageWriteArgs) == 120, "device storage write ABI changed");
static_assert(sizeof(NdsStorageBatchReadArgs) == 120, "device storage batch-read ABI changed");
static_assert(sizeof(NdsStorageBatchWriteArgs) == 120, "device storage batch-write ABI changed");
static_assert(sizeof(NdsStorageWaitArgs) == 88, "device storage wait ABI changed");
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
