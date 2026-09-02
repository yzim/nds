#ifndef NDS_STORAGE_PROTOCOL_HH
#define NDS_STORAGE_PROTOCOL_HH

#include <stddef.h>
#include <stdint.h>

#ifndef NDS_STORAGE_SERDE_INLINE
#define NDS_STORAGE_SERDE_INLINE inline
#define NDS_STORAGE_SERDE_INLINE_LOCAL
#endif

namespace nds {

inline constexpr uint32_t kStorageBootstrapBytes = 96U;
inline constexpr uint32_t kStorageNamespaceBytes = 16U;
inline constexpr uint32_t kStorageCommandBytes = 64U;
inline constexpr uint32_t kStorageBatchEntryBytes = 40U;
inline constexpr uint32_t kStorageCompletionBytes = 32U;
inline constexpr uint32_t kStorageMaxBatchEntries = 1024U;

enum class StorageOperation : uint16_t {
    Read = 1U,
    Write = 2U,
    BatchRead = 3U,
    BatchWrite = 4U,
};

enum class StorageCompletionState : uint16_t {
    Pending = 0U,
    Complete = 1U,
};

enum class StorageStatus : uint16_t {
    Success = 0U,
    InvalidCommand = 1U,
    RangeError = 2U,
    TransportError = 3U,
    InternalError = 4U,
};

enum class StorageSerdeResult {
    Ok,
    InvalidArgument,
    InvalidRecord,
};

struct StorageMemory {
    uint64_t address;
    uint64_t length;
    uint32_t remote_key;
};

struct StorageReadCommand {
    uint64_t command_id;
    uint64_t offset;
    uint64_t length;
    StorageMemory data;
    uint32_t slot_index{};
};

struct StorageWriteCommand {
    uint64_t command_id;
    uint64_t offset;
    uint64_t length;
    StorageMemory data;
    uint32_t slot_index{};
};

struct StorageBatchReadCommand {
    uint64_t command_id;
    uint64_t entry_count;
    uint64_t total_length;
    StorageMemory entries;
    uint32_t slot_index{};
};

struct StorageBatchWriteCommand {
    uint64_t command_id;
    uint64_t entry_count;
    uint64_t total_length;
    StorageMemory entries;
    uint32_t slot_index{};
};

struct StorageBatchReadEntry {
    uint64_t offset;
    uint64_t length;
    StorageMemory data;
};

struct StorageBatchWriteEntry {
    uint64_t offset;
    uint64_t length;
    StorageMemory data;
};

struct StorageCompletion {
    uint64_t command_id;
    StorageCompletionState state;
    StorageStatus status;
    uint64_t bytes_transferred;
};

struct StorageBootstrap {
    StorageMemory completion;
    StorageMemory namespace_response;
    StorageMemory slots;
    uint32_t slot_count{};
};

struct StorageSlot {
    StorageMemory command;
    StorageMemory completion;
    uint32_t qp_index{};
    uint32_t reserved{};
};

static_assert(sizeof(StorageSlot) == 56, "storage slot wire record size must match its descriptor size");

struct StorageNamespace {
    uint64_t capacity;
};

namespace storage_serde_detail {

inline constexpr uint32_t kBootstrapMagic = UINT32_C(0x4e445342);   // "NDSB"
inline constexpr uint32_t kCommandMagic = UINT32_C(0x4e445343);     // "NDSC"
inline constexpr uint32_t kCompletionMagic = UINT32_C(0x4e445344);  // "NDSD"
inline constexpr uint32_t kNamespaceMagic = UINT32_C(0x4e44534e);   // "NDSN"
inline constexpr uint16_t kVersion = 3U;
inline constexpr uint16_t kBootstrapVersion = 5U;
inline constexpr uint32_t kStorageSlotDescriptorBytes = 56U;
inline constexpr uint32_t kRemoteWrite = UINT32_C(0x00000002);
inline constexpr uint32_t kRemoteRead = UINT32_C(0x00000004);

NDS_STORAGE_SERDE_INLINE void clear(uint8_t *bytes, uint32_t size) {
    for (uint32_t index = 0U; index < size; ++index) bytes[index] = 0U;
}

NDS_STORAGE_SERDE_INLINE void write_u16(uint8_t *bytes, uint16_t value) {
    bytes[0] = static_cast<uint8_t>(value >> 8U);
    bytes[1] = static_cast<uint8_t>(value);
}

NDS_STORAGE_SERDE_INLINE void write_u32(uint8_t *bytes, uint32_t value) {
    bytes[0] = static_cast<uint8_t>(value >> 24U);
    bytes[1] = static_cast<uint8_t>(value >> 16U);
    bytes[2] = static_cast<uint8_t>(value >> 8U);
    bytes[3] = static_cast<uint8_t>(value);
}

NDS_STORAGE_SERDE_INLINE void write_u64(uint8_t *bytes, uint64_t value) {
    write_u32(bytes, static_cast<uint32_t>(value >> 32U));
    write_u32(bytes + 4U, static_cast<uint32_t>(value));
}

NDS_STORAGE_SERDE_INLINE uint16_t read_u16(const uint8_t *bytes) {
    const volatile uint8_t *source = bytes;
    return static_cast<uint16_t>((static_cast<uint16_t>(source[0]) << 8U) | source[1]);
}

NDS_STORAGE_SERDE_INLINE uint32_t read_u32(const uint8_t *bytes) {
    const volatile uint8_t *source = bytes;
    return (static_cast<uint32_t>(source[0]) << 24U) | (static_cast<uint32_t>(source[1]) << 16U) |
           (static_cast<uint32_t>(source[2]) << 8U) | source[3];
}

NDS_STORAGE_SERDE_INLINE uint64_t read_u64(const uint8_t *bytes) {
    return (static_cast<uint64_t>(read_u32(bytes)) << 32U) | read_u32(bytes + 4U);
}

NDS_STORAGE_SERDE_INLINE bool memory_valid(const StorageMemory &memory, uint64_t required_length) {
    return memory.address != 0U && memory.length >= required_length && memory.remote_key != 0U;
}

NDS_STORAGE_SERDE_INLINE StorageSerdeResult serialize_command(StorageOperation operation, uint64_t command_id,
                                                              uint64_t offset, uint64_t length,
                                                              const StorageMemory &data, uint64_t required_data_length,
                                                              uint32_t access, uint32_t slot_index, uint8_t *bytes,
                                                              uint32_t size) {
    if (bytes == nullptr || size < kStorageCommandBytes)
        return StorageSerdeResult::InvalidArgument;
    if (command_id == 0U || length == 0U || !memory_valid(data, required_data_length))
        return StorageSerdeResult::InvalidRecord;
    clear(bytes, kStorageCommandBytes);
    write_u32(bytes, kCommandMagic);
    write_u16(bytes + 4U, kVersion);
    write_u16(bytes + 6U, static_cast<uint16_t>(operation));
    write_u64(bytes + 8U, command_id);
    write_u64(bytes + 16U, offset);
    write_u64(bytes + 24U, length);
    write_u64(bytes + 32U, data.address);
    write_u64(bytes + 40U, data.length);
    write_u32(bytes + 48U, data.remote_key);
    write_u32(bytes + 52U, access);
    write_u32(bytes + 56U, slot_index);
    return StorageSerdeResult::Ok;
}

NDS_STORAGE_SERDE_INLINE StorageSerdeResult deserialize_command(const uint8_t *bytes, uint32_t size,
                                                                StorageOperation expected_operation,
                                                                uint32_t required_access, uint64_t *command_id,
                                                                uint64_t *offset, uint64_t *length, StorageMemory *data,
                                                                uint32_t *slot_index) {
    if (bytes == nullptr || command_id == nullptr || offset == nullptr || length == nullptr || data == nullptr ||
        slot_index == nullptr || size < kStorageCommandBytes)
        return StorageSerdeResult::InvalidArgument;
    if (read_u32(bytes) != kCommandMagic || read_u16(bytes + 4U) != kVersion ||
        read_u16(bytes + 6U) != static_cast<uint16_t>(expected_operation) ||
        (read_u32(bytes + 52U) & required_access) != required_access)
        return StorageSerdeResult::InvalidRecord;
    *command_id = read_u64(bytes + 8U);
    *offset = read_u64(bytes + 16U);
    *length = read_u64(bytes + 24U);
    *data = {read_u64(bytes + 32U), read_u64(bytes + 40U), read_u32(bytes + 48U)};
    *slot_index = read_u32(bytes + 56U);
    if (*command_id == 0U || *length == 0U || !memory_valid(*data, 0U) || read_u32(bytes + 60U) != 0U)
        return StorageSerdeResult::InvalidRecord;
    return StorageSerdeResult::Ok;
}

NDS_STORAGE_SERDE_INLINE StorageSerdeResult serialize_batch_entry(uint64_t offset, uint64_t length,
                                                                  const StorageMemory &data, uint32_t access,
                                                                  uint8_t *bytes, uint32_t size) {
    if (bytes == nullptr || size < kStorageBatchEntryBytes)
        return StorageSerdeResult::InvalidArgument;
    if (length == 0U || !memory_valid(data, length))
        return StorageSerdeResult::InvalidRecord;
    clear(bytes, kStorageBatchEntryBytes);
    write_u64(bytes, offset);
    write_u64(bytes + 8U, length);
    write_u64(bytes + 16U, data.address);
    write_u64(bytes + 24U, data.length);
    write_u32(bytes + 32U, data.remote_key);
    write_u32(bytes + 36U, access);
    return StorageSerdeResult::Ok;
}

NDS_STORAGE_SERDE_INLINE StorageSerdeResult deserialize_batch_entry(const uint8_t *bytes, uint32_t size,
                                                                    uint32_t required_access, uint64_t *offset,
                                                                    uint64_t *length, StorageMemory *data) {
    if (bytes == nullptr || offset == nullptr || length == nullptr || data == nullptr || size < kStorageBatchEntryBytes)
        return StorageSerdeResult::InvalidArgument;
    if ((read_u32(bytes + 36U) & required_access) != required_access)
        return StorageSerdeResult::InvalidRecord;
    *offset = read_u64(bytes);
    *length = read_u64(bytes + 8U);
    *data = {read_u64(bytes + 16U), read_u64(bytes + 24U), read_u32(bytes + 32U)};
    if (*length == 0U || !memory_valid(*data, *length))
        return StorageSerdeResult::InvalidRecord;
    return StorageSerdeResult::Ok;
}

}  // namespace storage_serde_detail

NDS_STORAGE_SERDE_INLINE StorageSerdeResult serialize_storage_read(const StorageReadCommand &command, uint8_t *bytes,
                                                                   uint32_t size) {
    return storage_serde_detail::serialize_command(StorageOperation::Read, command.command_id, command.offset,
                                                   command.length, command.data, command.length,
                                                   storage_serde_detail::kRemoteWrite, command.slot_index, bytes, size);
}

NDS_STORAGE_SERDE_INLINE StorageSerdeResult deserialize_storage_read(const uint8_t *bytes, uint32_t size,
                                                                     StorageReadCommand *command) {
    if (command == nullptr)
        return StorageSerdeResult::InvalidArgument;
    const StorageSerdeResult result = storage_serde_detail::deserialize_command(
        bytes, size, StorageOperation::Read, storage_serde_detail::kRemoteWrite, &command->command_id, &command->offset,
        &command->length, &command->data, &command->slot_index);
    if (result != StorageSerdeResult::Ok)
        return result;
    return command->data.length >= command->length ? StorageSerdeResult::Ok : StorageSerdeResult::InvalidRecord;
}

NDS_STORAGE_SERDE_INLINE StorageSerdeResult serialize_storage_write(const StorageWriteCommand &command, uint8_t *bytes,
                                                                    uint32_t size) {
    return storage_serde_detail::serialize_command(StorageOperation::Write, command.command_id, command.offset,
                                                   command.length, command.data, command.length,
                                                   storage_serde_detail::kRemoteRead, command.slot_index, bytes, size);
}

NDS_STORAGE_SERDE_INLINE StorageSerdeResult deserialize_storage_write(const uint8_t *bytes, uint32_t size,
                                                                      StorageWriteCommand *command) {
    if (command == nullptr)
        return StorageSerdeResult::InvalidArgument;
    const StorageSerdeResult result = storage_serde_detail::deserialize_command(
        bytes, size, StorageOperation::Write, storage_serde_detail::kRemoteRead, &command->command_id, &command->offset,
        &command->length, &command->data, &command->slot_index);
    if (result != StorageSerdeResult::Ok)
        return result;
    return command->data.length >= command->length ? StorageSerdeResult::Ok : StorageSerdeResult::InvalidRecord;
}

NDS_STORAGE_SERDE_INLINE StorageSerdeResult serialize_storage_batch_read(const StorageBatchReadCommand &command,
                                                                         uint8_t *bytes, uint32_t size) {
    if (command.entry_count == 0U || command.entry_count > kStorageMaxBatchEntries ||
        command.entries.length < command.entry_count * kStorageBatchEntryBytes)
        return StorageSerdeResult::InvalidRecord;
    return storage_serde_detail::serialize_command(StorageOperation::BatchRead, command.command_id, command.entry_count,
                                                   command.total_length, command.entries,
                                                   command.entry_count * kStorageBatchEntryBytes,
                                                   storage_serde_detail::kRemoteRead, command.slot_index, bytes, size);
}

NDS_STORAGE_SERDE_INLINE StorageSerdeResult deserialize_storage_batch_read(const uint8_t *bytes, uint32_t size,
                                                                           StorageBatchReadCommand *command) {
    if (command == nullptr)
        return StorageSerdeResult::InvalidArgument;
    const StorageSerdeResult result = storage_serde_detail::deserialize_command(
        bytes, size, StorageOperation::BatchRead, storage_serde_detail::kRemoteRead, &command->command_id,
        &command->entry_count, &command->total_length, &command->entries, &command->slot_index);
    if (result != StorageSerdeResult::Ok)
        return result;
    return command->entry_count <= kStorageMaxBatchEntries &&
                   command->entries.length >= command->entry_count * kStorageBatchEntryBytes
               ? StorageSerdeResult::Ok
               : StorageSerdeResult::InvalidRecord;
}

NDS_STORAGE_SERDE_INLINE StorageSerdeResult serialize_storage_batch_write(const StorageBatchWriteCommand &command,
                                                                          uint8_t *bytes, uint32_t size) {
    if (command.entry_count == 0U || command.entry_count > kStorageMaxBatchEntries ||
        command.entries.length < command.entry_count * kStorageBatchEntryBytes)
        return StorageSerdeResult::InvalidRecord;
    return storage_serde_detail::serialize_command(StorageOperation::BatchWrite, command.command_id,
                                                   command.entry_count, command.total_length, command.entries,
                                                   command.entry_count * kStorageBatchEntryBytes,
                                                   storage_serde_detail::kRemoteRead, command.slot_index, bytes, size);
}

NDS_STORAGE_SERDE_INLINE StorageSerdeResult deserialize_storage_batch_write(const uint8_t *bytes, uint32_t size,
                                                                            StorageBatchWriteCommand *command) {
    if (command == nullptr)
        return StorageSerdeResult::InvalidArgument;
    const StorageSerdeResult result = storage_serde_detail::deserialize_command(
        bytes, size, StorageOperation::BatchWrite, storage_serde_detail::kRemoteRead, &command->command_id,
        &command->entry_count, &command->total_length, &command->entries, &command->slot_index);
    if (result != StorageSerdeResult::Ok)
        return result;
    return command->entry_count <= kStorageMaxBatchEntries &&
                   command->entries.length >= command->entry_count * kStorageBatchEntryBytes
               ? StorageSerdeResult::Ok
               : StorageSerdeResult::InvalidRecord;
}

NDS_STORAGE_SERDE_INLINE StorageSerdeResult serialize_storage_batch_read_entry(const StorageBatchReadEntry &entry,
                                                                               uint8_t *bytes, uint32_t size) {
    return storage_serde_detail::serialize_batch_entry(entry.offset, entry.length, entry.data,
                                                       storage_serde_detail::kRemoteWrite, bytes, size);
}

NDS_STORAGE_SERDE_INLINE StorageSerdeResult deserialize_storage_batch_read_entry(const uint8_t *bytes, uint32_t size,
                                                                                 StorageBatchReadEntry *entry) {
    if (entry == nullptr)
        return StorageSerdeResult::InvalidArgument;
    return storage_serde_detail::deserialize_batch_entry(bytes, size, storage_serde_detail::kRemoteWrite,
                                                         &entry->offset, &entry->length, &entry->data);
}

NDS_STORAGE_SERDE_INLINE StorageSerdeResult serialize_storage_batch_write_entry(const StorageBatchWriteEntry &entry,
                                                                                uint8_t *bytes, uint32_t size) {
    return storage_serde_detail::serialize_batch_entry(entry.offset, entry.length, entry.data,
                                                       storage_serde_detail::kRemoteRead, bytes, size);
}

NDS_STORAGE_SERDE_INLINE StorageSerdeResult deserialize_storage_batch_write_entry(const uint8_t *bytes, uint32_t size,
                                                                                  StorageBatchWriteEntry *entry) {
    if (entry == nullptr)
        return StorageSerdeResult::InvalidArgument;
    return storage_serde_detail::deserialize_batch_entry(bytes, size, storage_serde_detail::kRemoteRead, &entry->offset,
                                                         &entry->length, &entry->data);
}

NDS_STORAGE_SERDE_INLINE StorageSerdeResult deserialize_storage_operation(const uint8_t *bytes, uint32_t size,
                                                                          StorageOperation *operation) {
    if (bytes == nullptr || operation == nullptr || size < kStorageCommandBytes)
        return StorageSerdeResult::InvalidArgument;
    if (storage_serde_detail::read_u32(bytes) != storage_serde_detail::kCommandMagic ||
        storage_serde_detail::read_u16(bytes + 4U) != storage_serde_detail::kVersion)
        return StorageSerdeResult::InvalidRecord;
    const uint16_t value = storage_serde_detail::read_u16(bytes + 6U);
    if (value < static_cast<uint16_t>(StorageOperation::Read) ||
        value > static_cast<uint16_t>(StorageOperation::BatchWrite))
        return StorageSerdeResult::InvalidRecord;
    *operation = static_cast<StorageOperation>(value);
    return StorageSerdeResult::Ok;
}

NDS_STORAGE_SERDE_INLINE StorageSerdeResult serialize_storage_completion(const StorageCompletion &completion,
                                                                         uint8_t *bytes, uint32_t size) {
    if (bytes == nullptr || size < kStorageCompletionBytes)
        return StorageSerdeResult::InvalidArgument;
    if (completion.command_id == 0U ||
        static_cast<uint16_t>(completion.status) > static_cast<uint16_t>(StorageStatus::InternalError) ||
        (completion.state != StorageCompletionState::Pending && completion.state != StorageCompletionState::Complete) ||
        (completion.state == StorageCompletionState::Pending &&
         (completion.status != StorageStatus::Success || completion.bytes_transferred != 0U)))
        return StorageSerdeResult::InvalidRecord;
    storage_serde_detail::clear(bytes, kStorageCompletionBytes);
    storage_serde_detail::write_u32(bytes, storage_serde_detail::kCompletionMagic);
    storage_serde_detail::write_u16(bytes + 4U, storage_serde_detail::kVersion);
    storage_serde_detail::write_u16(bytes + 6U, static_cast<uint16_t>(completion.state));
    storage_serde_detail::write_u16(bytes + 8U, static_cast<uint16_t>(completion.status));
    storage_serde_detail::write_u64(bytes + 12U, completion.command_id);
    storage_serde_detail::write_u64(bytes + 20U, completion.bytes_transferred);
    return StorageSerdeResult::Ok;
}

NDS_STORAGE_SERDE_INLINE StorageSerdeResult deserialize_storage_completion(const uint8_t *bytes, uint32_t size,
                                                                           StorageCompletion *completion) {
    if (bytes == nullptr || completion == nullptr || size < kStorageCompletionBytes)
        return StorageSerdeResult::InvalidArgument;
    if (storage_serde_detail::read_u32(bytes) != storage_serde_detail::kCompletionMagic ||
        storage_serde_detail::read_u16(bytes + 4U) != storage_serde_detail::kVersion)
        return StorageSerdeResult::InvalidRecord;
    *completion = {storage_serde_detail::read_u64(bytes + 12U),
                   static_cast<StorageCompletionState>(storage_serde_detail::read_u16(bytes + 6U)),
                   static_cast<StorageStatus>(storage_serde_detail::read_u16(bytes + 8U)),
                   storage_serde_detail::read_u64(bytes + 20U)};
    if (completion->command_id == 0U ||
        static_cast<uint16_t>(completion->status) > static_cast<uint16_t>(StorageStatus::InternalError) ||
        (completion->state != StorageCompletionState::Pending &&
         completion->state != StorageCompletionState::Complete) ||
        (completion->state == StorageCompletionState::Pending &&
         (completion->status != StorageStatus::Success || completion->bytes_transferred != 0U)))
        return StorageSerdeResult::InvalidRecord;
    return StorageSerdeResult::Ok;
}

NDS_STORAGE_SERDE_INLINE StorageSerdeResult serialize_storage_bootstrap(const StorageBootstrap &bootstrap,
                                                                        uint8_t *bytes, uint32_t size) {
    if (bytes == nullptr || size < kStorageBootstrapBytes)
        return StorageSerdeResult::InvalidArgument;
    if (!storage_serde_detail::memory_valid(bootstrap.completion, kStorageCompletionBytes) ||
        !storage_serde_detail::memory_valid(bootstrap.namespace_response, kStorageNamespaceBytes) ||
        bootstrap.slot_count == 0U ||
        bootstrap.slot_count > UINT32_MAX / storage_serde_detail::kStorageSlotDescriptorBytes ||
        !storage_serde_detail::memory_valid(bootstrap.slots, static_cast<uint64_t>(bootstrap.slot_count) *
                                                                 storage_serde_detail::kStorageSlotDescriptorBytes))
        return StorageSerdeResult::InvalidRecord;
    storage_serde_detail::clear(bytes, kStorageBootstrapBytes);
    storage_serde_detail::write_u32(bytes, storage_serde_detail::kBootstrapMagic);
    storage_serde_detail::write_u16(bytes + 4U, storage_serde_detail::kBootstrapVersion);
    storage_serde_detail::write_u64(bytes + 8U, bootstrap.completion.address);
    storage_serde_detail::write_u64(bytes + 16U, bootstrap.completion.length);
    storage_serde_detail::write_u32(bytes + 24U, bootstrap.completion.remote_key);
    storage_serde_detail::write_u32(bytes + 28U, storage_serde_detail::kRemoteWrite);
    storage_serde_detail::write_u64(bytes + 32U, bootstrap.namespace_response.address);
    storage_serde_detail::write_u64(bytes + 40U, bootstrap.namespace_response.length);
    storage_serde_detail::write_u32(bytes + 48U, bootstrap.namespace_response.remote_key);
    storage_serde_detail::write_u32(bytes + 52U, storage_serde_detail::kRemoteWrite);
    storage_serde_detail::write_u64(bytes + 56U, bootstrap.slots.address);
    storage_serde_detail::write_u64(bytes + 64U, bootstrap.slots.length);
    storage_serde_detail::write_u32(bytes + 72U, bootstrap.slots.remote_key);
    storage_serde_detail::write_u32(bytes + 76U, storage_serde_detail::kRemoteRead);
    storage_serde_detail::write_u32(bytes + 80U, bootstrap.slot_count);
    return StorageSerdeResult::Ok;
}

NDS_STORAGE_SERDE_INLINE StorageSerdeResult deserialize_storage_bootstrap(const uint8_t *bytes, uint32_t size,
                                                                          StorageBootstrap *bootstrap) {
    if (bytes == nullptr || bootstrap == nullptr || size < kStorageBootstrapBytes)
        return StorageSerdeResult::InvalidArgument;
    if (storage_serde_detail::read_u32(bytes) != storage_serde_detail::kBootstrapMagic ||
        storage_serde_detail::read_u16(bytes + 4U) != storage_serde_detail::kBootstrapVersion ||
        (storage_serde_detail::read_u32(bytes + 28U) & storage_serde_detail::kRemoteWrite) == 0U ||
        (storage_serde_detail::read_u32(bytes + 76U) & storage_serde_detail::kRemoteRead) == 0U)
        return StorageSerdeResult::InvalidRecord;
    bootstrap->completion = {storage_serde_detail::read_u64(bytes + 8U), storage_serde_detail::read_u64(bytes + 16U),
                             storage_serde_detail::read_u32(bytes + 24U)};
    bootstrap->namespace_response = {storage_serde_detail::read_u64(bytes + 32U),
                                     storage_serde_detail::read_u64(bytes + 40U),
                                     storage_serde_detail::read_u32(bytes + 48U)};
    bootstrap->slots = {storage_serde_detail::read_u64(bytes + 56U), storage_serde_detail::read_u64(bytes + 64U),
                        storage_serde_detail::read_u32(bytes + 72U)};
    bootstrap->slot_count = storage_serde_detail::read_u32(bytes + 80U);
    const uint32_t namespace_access = storage_serde_detail::read_u32(bytes + 52U);
    return (namespace_access & storage_serde_detail::kRemoteWrite) != 0U && bootstrap->slot_count != 0U &&
                   bootstrap->slot_count <= UINT32_MAX / storage_serde_detail::kStorageSlotDescriptorBytes &&
                   storage_serde_detail::memory_valid(bootstrap->namespace_response, kStorageNamespaceBytes) &&
                   storage_serde_detail::memory_valid(bootstrap->completion, kStorageCompletionBytes) &&
                   storage_serde_detail::memory_valid(
                       bootstrap->slots,
                       static_cast<uint64_t>(bootstrap->slot_count) * storage_serde_detail::kStorageSlotDescriptorBytes)
               ? StorageSerdeResult::Ok
               : StorageSerdeResult::InvalidRecord;
}

NDS_STORAGE_SERDE_INLINE StorageSerdeResult serialize_storage_namespace(const StorageNamespace &storage_namespace,
                                                                        uint8_t *bytes, uint32_t size) {
    if (bytes == nullptr || size < kStorageNamespaceBytes)
        return StorageSerdeResult::InvalidArgument;
    if (storage_namespace.capacity == 0U)
        return StorageSerdeResult::InvalidRecord;
    storage_serde_detail::clear(bytes, kStorageNamespaceBytes);
    storage_serde_detail::write_u32(bytes, storage_serde_detail::kNamespaceMagic);
    storage_serde_detail::write_u16(bytes + 4U, storage_serde_detail::kVersion);
    storage_serde_detail::write_u64(bytes + 8U, storage_namespace.capacity);
    return StorageSerdeResult::Ok;
}

NDS_STORAGE_SERDE_INLINE StorageSerdeResult deserialize_storage_namespace(const uint8_t *bytes, uint32_t size,
                                                                          StorageNamespace *storage_namespace) {
    if (bytes == nullptr || storage_namespace == nullptr || size < kStorageNamespaceBytes)
        return StorageSerdeResult::InvalidArgument;
    if (storage_serde_detail::read_u32(bytes) != storage_serde_detail::kNamespaceMagic ||
        storage_serde_detail::read_u16(bytes + 4U) != storage_serde_detail::kVersion)
        return StorageSerdeResult::InvalidRecord;
    storage_namespace->capacity = storage_serde_detail::read_u64(bytes + 8U);
    return storage_namespace->capacity != 0U ? StorageSerdeResult::Ok : StorageSerdeResult::InvalidRecord;
}

}  // namespace nds

#ifdef NDS_STORAGE_SERDE_INLINE_LOCAL
#undef NDS_STORAGE_SERDE_INLINE_LOCAL
#undef NDS_STORAGE_SERDE_INLINE
#endif

#endif
