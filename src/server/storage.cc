#include "storage.hh"

#include "storage_protocol.hh"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace nds::server {
namespace {

Result<StorageBootstrap> exchange_bootstrap(Transport *transport, const uint8_t *bootstrap_bytes,
                                            std::uint64_t capacity) {
    uint8_t namespace_bytes[kStorageNamespaceBytes]{};
    StorageBootstrap bootstrap{};
    if (deserialize_storage_bootstrap(bootstrap_bytes, kStorageBootstrapBytes, &bootstrap) != StorageSerdeResult::Ok ||
        serialize_storage_namespace({capacity}, namespace_bytes, sizeof(namespace_bytes)) != StorageSerdeResult::Ok)
        return Error{ErrorCode::kProtocol, "invalid storage bootstrap record"};
    auto namespace_region =
        transport->register_memory(namespace_bytes, sizeof(namespace_bytes), MemoryAccess::LocalRead);
    if (!namespace_region.ok())
        return namespace_region.error();
    if (const auto completed = transport->write(namespace_region.value(), bootstrap.namespace_response.address,
                                                bootstrap.namespace_response.remote_key, sizeof(namespace_bytes));
        !completed.ok()) {
        return completed.error();
    }
    return bootstrap;
}

Result<void> move_data(Transport *transport, std::vector<unsigned char> *storage, std::uint64_t offset,
                       std::uint64_t length, const StorageMemory &remote, bool read) {
    auto *data = storage->data() + offset;
    auto data_region = transport->register_memory(data, length, MemoryAccess::LocalWrite);
    if (!data_region.ok())
        return data_region.error();
    const auto transferred = read ? transport->write(data_region.value(), remote.address, remote.remote_key,
                                                     static_cast<std::uint32_t>(length))
                                  : transport->read(data_region.value(), remote.address, remote.remote_key,
                                                    static_cast<std::uint32_t>(length));
    if (!transferred.ok())
        return transferred.error();
    return {};
}

template <typename Command, typename Entry, typename DeserializeEntry>
Result<void> process_batch(Transport *transport, std::vector<unsigned char> *storage, const Command &command, bool read,
                           DeserializeEntry deserialize_entry, StorageCompletion *completion) {
    const auto count = static_cast<std::size_t>(command.entry_count);
    std::vector<uint8_t> entry_bytes(count * kStorageBatchEntryBytes);
    auto entry_region = transport->register_memory(entry_bytes.data(), entry_bytes.size(), MemoryAccess::LocalWrite);
    if (!entry_region.ok())
        return entry_region.error();
    if (const auto fetched = transport->read(entry_region.value(), command.entries.address, command.entries.remote_key,
                                             static_cast<std::uint32_t>(entry_bytes.size()));
        !fetched.ok())
        return fetched.error();

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
        if (const auto moved = move_data(transport, storage, entry.offset, entry.length, entry.data, read); !moved.ok())
            return moved.error();
    }
    completion->bytes_transferred = total_length;
    return {};
}

template <typename Command>
Result<void> process_single(Transport *transport, std::vector<unsigned char> *storage, const Command &command,
                            bool read, StorageCompletion *completion) {
    if (command.offset > storage->size() || command.length > storage->size() - command.offset ||
        command.length > std::numeric_limits<std::uint32_t>::max()) {
        completion->status = StorageStatus::RangeError;
        return {};
    }
    if (const auto moved = move_data(transport, storage, command.offset, command.length, command.data, read);
        !moved.ok())
        return moved.error();
    completion->bytes_transferred = command.length;
    return {};
}

}  // namespace

Result<void> serve_commands(Transport *transport, std::vector<unsigned char> *storage, std::uint32_t command_count,
                            std::uint32_t timeout_ms) {
    if (transport == nullptr || storage == nullptr || command_count == 0U)
        return Error{ErrorCode::kInvalidArgument, "transport and namespace are required"};
    uint8_t command_bytes[kStorageCommandBytes]{};
    uint8_t completion_bytes[kStorageCompletionBytes]{};
    uint8_t bootstrap_bytes[kStorageBootstrapBytes]{};
    auto bootstrap_region =
        transport->register_memory(bootstrap_bytes, sizeof(bootstrap_bytes), MemoryAccess::LocalWrite);
    if (!bootstrap_region.ok())
        return bootstrap_region.error();
    NDS_RETURN_IF_ERROR(transport->post_receive(bootstrap_region.value()));
    auto completion_region =
        transport->register_memory(completion_bytes, sizeof(completion_bytes), MemoryAccess::LocalRead);
    if (!completion_region.ok())
        return completion_region.error();
    if (const auto received = transport->wait_receive(timeout_ms); !received.ok())
        return received.error();
    const auto bootstrap = exchange_bootstrap(transport, bootstrap_bytes, storage->size());
    if (!bootstrap.ok())
        return bootstrap.error();
    for (std::uint32_t command_index = 0U; command_index < command_count; ++command_index) {
        auto command_region =
            transport->register_memory(command_bytes, sizeof(command_bytes), MemoryAccess::LocalWrite);
        if (!command_region.ok())
            return command_region.error();
        NDS_RETURN_IF_ERROR(transport->post_receive(command_region.value()));
        if (const auto result = transport->wait_receive(timeout_ms); !result.ok())
            return result.error();

        StorageOperation operation{};
        if (deserialize_storage_operation(command_bytes, sizeof(command_bytes), &operation) != StorageSerdeResult::Ok)
            return Error{ErrorCode::kProtocol, "invalid storage operation"};
        StorageCompletion completion{0U, StorageCompletionState::Complete, StorageStatus::Success, 0U};
        Result<void> processed;
        switch (operation) {
            case StorageOperation::Read: {
                StorageReadCommand command{};
                if (deserialize_storage_read(command_bytes, sizeof(command_bytes), &command) != StorageSerdeResult::Ok)
                    return Error{ErrorCode::kProtocol, "invalid storage read command"};
                completion.command_id = command.command_id;
                processed = process_single(transport, storage, command, true, &completion);
                break;
            }
            case StorageOperation::Write: {
                StorageWriteCommand command{};
                if (deserialize_storage_write(command_bytes, sizeof(command_bytes), &command) != StorageSerdeResult::Ok)
                    return Error{ErrorCode::kProtocol, "invalid storage write command"};
                completion.command_id = command.command_id;
                processed = process_single(transport, storage, command, false, &completion);
                break;
            }
            case StorageOperation::BatchRead: {
                StorageBatchReadCommand command{};
                if (deserialize_storage_batch_read(command_bytes, sizeof(command_bytes), &command) !=
                    StorageSerdeResult::Ok)
                    return Error{ErrorCode::kProtocol, "invalid storage batch-read command"};
                completion.command_id = command.command_id;
                processed = process_batch<StorageBatchReadCommand, StorageBatchReadEntry>(
                    transport, storage, command, true, deserialize_storage_batch_read_entry, &completion);
                break;
            }
            case StorageOperation::BatchWrite: {
                StorageBatchWriteCommand command{};
                if (deserialize_storage_batch_write(command_bytes, sizeof(command_bytes), &command) !=
                    StorageSerdeResult::Ok)
                    return Error{ErrorCode::kProtocol, "invalid storage batch-write command"};
                completion.command_id = command.command_id;
                processed = process_batch<StorageBatchWriteCommand, StorageBatchWriteEntry>(
                    transport, storage, command, false, deserialize_storage_batch_write_entry, &completion);
                break;
            }
        }
        if (!processed.ok())
            return processed.error();
        if (serialize_storage_completion(completion, completion_bytes, sizeof(completion_bytes)) !=
            StorageSerdeResult::Ok)
            return Error{ErrorCode::kProtocol, "invalid storage completion"};
        if (const auto completed = transport->write(completion_region.value(), bootstrap.value().completion.address,
                                                    bootstrap.value().completion.remote_key, sizeof(completion_bytes));
            !completed.ok())
            return completed.error();
    }
    return {};
}

}  // namespace nds::server
