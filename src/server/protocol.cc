#include "protocol.hh"

#include "nds/storage_protocol.hh"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace nds::server {
namespace {

Result<StorageBootstrap> exchange_bootstrap(Connection *connection, std::uint64_t capacity) {
    uint8_t bootstrap_bytes[kStorageBootstrapBytes]{};
    uint8_t namespace_bytes[kStorageNamespaceBytes]{};
    if (const auto received = connection->bootstrap()->receive_bytes(bootstrap_bytes, sizeof(bootstrap_bytes));
        !received)
        return unexpected(received.error());
    StorageBootstrap bootstrap{};
    if (deserialize_storage_bootstrap(bootstrap_bytes, sizeof(bootstrap_bytes), &bootstrap) !=
            StorageSerdeResult::Ok ||
        serialize_storage_namespace({capacity}, namespace_bytes, sizeof(namespace_bytes)) != StorageSerdeResult::Ok)
        return unexpected(ErrorCode::kProtocol, "invalid storage bootstrap record");
    if (const auto sent = connection->bootstrap()->send_bytes(namespace_bytes, sizeof(namespace_bytes)); !sent)
        return unexpected(sent.error());
    return bootstrap;
}

Result<void> move_data(Connection *connection, std::vector<unsigned char> *storage, std::uint64_t offset,
                       std::uint64_t length, const StorageMemory &remote, bool read) {
    auto *data = storage->data() + offset;
    auto data_region = connection->register_memory(data, length, MemoryAccess::LocalWrite);
    if (!data_region)
        return unexpected(data_region.error());
    const auto transferred = read ? connection->write(*data_region, remote.address, remote.remote_key,
                                                       static_cast<std::uint32_t>(length))
                                  : connection->read(*data_region, remote.address, remote.remote_key,
                                                     static_cast<std::uint32_t>(length));
    if (!transferred)
        return unexpected(transferred.error());
    return {};
}

template <typename Command, typename Entry, typename DeserializeEntry>
Result<void> process_batch(Connection *connection, std::vector<unsigned char> *storage, const Command &command,
                           bool read, DeserializeEntry deserialize_entry, StorageCompletion *completion) {
    const auto count = static_cast<std::size_t>(command.entry_count);
    std::vector<uint8_t> entry_bytes(count * kStorageBatchEntryBytes);
    auto entry_region =
        connection->register_memory(entry_bytes.data(), entry_bytes.size(), MemoryAccess::LocalWrite);
    if (!entry_region)
        return unexpected(entry_region.error());
    if (const auto fetched = connection->read(*entry_region, command.entries.address, command.entries.remote_key,
                                              static_cast<std::uint32_t>(entry_bytes.size()));
        !fetched)
        return unexpected(fetched.error());

    std::vector<Entry> entries(count);
    std::uint64_t total_length{};
    for (std::size_t index = 0U; index < count; ++index) {
        if (deserialize_entry(entry_bytes.data() + index * kStorageBatchEntryBytes, kStorageBatchEntryBytes,
                              &entries[index]) != StorageSerdeResult::Ok) {
            completion->status = StorageStatus::InvalidCommand;
            break;
        }
        if (entries[index].offset > storage->size() ||
            entries[index].length > storage->size() - entries[index].offset ||
            entries[index].length > std::numeric_limits<std::uint32_t>::max() ||
            entries[index].length > std::numeric_limits<std::uint64_t>::max() - total_length) {
            completion->status = StorageStatus::RangeError;
            break;
        }
        total_length += entries[index].length;
    }
    if (completion->status == StorageStatus::Success && total_length != command.total_length)
        completion->status = StorageStatus::InvalidCommand;
    if (completion->status != StorageStatus::Success)
        return {};
    for (const Entry &entry : entries) {
        if (const auto moved = move_data(connection, storage, entry.offset, entry.length, entry.data, read); !moved)
            return unexpected(moved.error());
    }
    completion->bytes_transferred = total_length;
    return {};
}

template <typename Command>
Result<void> process_single(Connection *connection, std::vector<unsigned char> *storage, const Command &command,
                            bool read, StorageCompletion *completion) {
    if (command.offset > storage->size() || command.length > storage->size() - command.offset ||
        command.length > std::numeric_limits<std::uint32_t>::max()) {
        completion->status = StorageStatus::RangeError;
        return {};
    }
    if (const auto moved = move_data(connection, storage, command.offset, command.length, command.data, read); !moved)
        return unexpected(moved.error());
    completion->bytes_transferred = command.length;
    return {};
}

}  // namespace

Result<void> serve_commands(Connection *connection, std::vector<unsigned char> *storage, std::uint32_t command_count,
                            std::uint32_t timeout_ms) {
    if (connection == nullptr || storage == nullptr || command_count == 0U)
        return unexpected(ErrorCode::kInvalidArgument, "connection and namespace are required");
    uint8_t command_bytes[kStorageCommandBytes]{};
    uint8_t completion_bytes[kStorageCompletionBytes]{};
    auto completion_region =
        connection->register_memory(completion_bytes, sizeof(completion_bytes), MemoryAccess::LocalRead);
    if (!completion_region)
        return unexpected(completion_region.error());
    if (const auto result = connection->activate(); !result)
        return unexpected(result.error());

    const auto bootstrap = exchange_bootstrap(connection, storage->size());
    if (!bootstrap)
        return unexpected(bootstrap.error());
    for (std::uint32_t command_index = 0U; command_index < command_count; ++command_index) {
        auto command_region = connection->prepare_receive(command_bytes, sizeof(command_bytes));
        if (!command_region)
            return unexpected(command_region.error());
        if (const auto result = connection->receive(timeout_ms); !result)
            return unexpected(result.error());

        StorageOperation operation{};
        if (deserialize_storage_operation(command_bytes, sizeof(command_bytes), &operation) != StorageSerdeResult::Ok)
            return unexpected(ErrorCode::kProtocol, "invalid storage operation");
        StorageCompletion completion{0U, StorageCompletionState::Complete, StorageStatus::Success, 0U};
        Result<void> processed;
        switch (operation) {
            case StorageOperation::Read: {
                StorageReadCommand command{};
                if (deserialize_storage_read(command_bytes, sizeof(command_bytes), &command) != StorageSerdeResult::Ok)
                    return unexpected(ErrorCode::kProtocol, "invalid storage read command");
                completion.command_id = command.command_id;
                processed = process_single(connection, storage, command, true, &completion);
                break;
            }
            case StorageOperation::Write: {
                StorageWriteCommand command{};
                if (deserialize_storage_write(command_bytes, sizeof(command_bytes), &command) !=
                    StorageSerdeResult::Ok)
                    return unexpected(ErrorCode::kProtocol, "invalid storage write command");
                completion.command_id = command.command_id;
                processed = process_single(connection, storage, command, false, &completion);
                break;
            }
            case StorageOperation::BatchRead: {
                StorageBatchReadCommand command{};
                if (deserialize_storage_batch_read(command_bytes, sizeof(command_bytes), &command) !=
                    StorageSerdeResult::Ok)
                    return unexpected(ErrorCode::kProtocol, "invalid storage batch-read command");
                completion.command_id = command.command_id;
                processed = process_batch<StorageBatchReadCommand, StorageBatchReadEntry>(
                    connection, storage, command, true, deserialize_storage_batch_read_entry, &completion);
                break;
            }
            case StorageOperation::BatchWrite: {
                StorageBatchWriteCommand command{};
                if (deserialize_storage_batch_write(command_bytes, sizeof(command_bytes), &command) !=
                    StorageSerdeResult::Ok)
                    return unexpected(ErrorCode::kProtocol, "invalid storage batch-write command");
                completion.command_id = command.command_id;
                processed = process_batch<StorageBatchWriteCommand, StorageBatchWriteEntry>(
                    connection, storage, command, false, deserialize_storage_batch_write_entry, &completion);
                break;
            }
        }
        if (!processed)
            return unexpected(processed.error());
        if (serialize_storage_completion(completion, completion_bytes, sizeof(completion_bytes)) !=
            StorageSerdeResult::Ok)
            return unexpected(ErrorCode::kProtocol, "invalid storage completion");
        if (const auto completed = connection->write(*completion_region, bootstrap->completion.address,
                                                     bootstrap->completion.remote_key, sizeof(completion_bytes));
            !completed)
            return unexpected(completed.error());
    }
    return {};
}

}  // namespace nds::server
