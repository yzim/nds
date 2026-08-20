#ifndef NDS_DEVICE_STORAGE_H
#define NDS_DEVICE_STORAGE_H

#include "nds/device_transport.h"
#include "nds/storage_protocol.hh"

#include <stdint.h>

#define NDS_DEVICE_STORAGE_ABI_VERSION UINT32_C(1)

typedef struct NdsDeviceStorageContext {
    uint32_t abi_version;
    uint32_t size;
    NdsDeviceTransport transport;
    NdsDeviceSge command_buffer;
    NdsDeviceSge completion;
    uint64_t capacity;
} NdsDeviceStorageContext;

typedef struct NdsDeviceStorageReadArgs {
    uint32_t abi_version;
    uint32_t size;
    NdsDeviceStorageContext context;
    nds::StorageReadCommand command;
    uint64_t operation_result_address;
} NdsDeviceStorageReadArgs;

typedef struct NdsDeviceStorageWriteArgs {
    uint32_t abi_version;
    uint32_t size;
    NdsDeviceStorageContext context;
    nds::StorageWriteCommand command;
    uint64_t operation_result_address;
} NdsDeviceStorageWriteArgs;

typedef struct NdsDeviceStorageBatchReadArgs {
    uint32_t abi_version;
    uint32_t size;
    NdsDeviceStorageContext context;
    nds::StorageBatchReadCommand command;
    uint64_t operation_result_address;
} NdsDeviceStorageBatchReadArgs;

typedef struct NdsDeviceStorageBatchWriteArgs {
    uint32_t abi_version;
    uint32_t size;
    NdsDeviceStorageContext context;
    nds::StorageBatchWriteCommand command;
    uint64_t operation_result_address;
} NdsDeviceStorageBatchWriteArgs;

typedef struct NdsDeviceStorageWaitArgs {
    uint32_t abi_version;
    uint32_t size;
    NdsDeviceStorageContext context;
    uint64_t command_id;
    uint64_t expected_bytes;
    uint64_t operation_result_address;
} NdsDeviceStorageWaitArgs;

static inline int nds_device_storage_context_valid(const NdsDeviceStorageContext *context) {
    return context != nullptr && context->abi_version == NDS_DEVICE_STORAGE_ABI_VERSION &&
           context->size == sizeof(*context) && context->transport.abi_version == NDS_DEVICE_TRANSPORT_ABI_VERSION &&
           context->transport.size == sizeof(context->transport) && context->command_buffer.address != 0U &&
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

static_assert(sizeof(NdsDeviceStorageContext) == 296, "device storage context ABI changed");
static_assert(sizeof(NdsDeviceStorageReadArgs) == 360, "device storage read ABI changed");
static_assert(sizeof(NdsDeviceStorageWriteArgs) == 360, "device storage write ABI changed");
static_assert(sizeof(NdsDeviceStorageBatchReadArgs) == 360, "device storage batch-read ABI changed");
static_assert(sizeof(NdsDeviceStorageBatchWriteArgs) == 360, "device storage batch-write ABI changed");
static_assert(sizeof(NdsDeviceStorageWaitArgs) == 328, "device storage wait ABI changed");

#endif
