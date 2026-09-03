#include "storage.hh"

#include "storage_protocol.hh"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cerrno>
#include <cstring>
#include <limits>
#include <mutex>
#include <optional>
#if defined(__linux__)
#include <pthread.h>
#include <sched.h>
#endif
#include <string>
#include <thread>
#include <vector>

namespace nds::server {
namespace {

Result<StorageBootstrap> exchange_bootstrap(Transport *transport, const std::uint8_t *bootstrap_bytes,
                                            std::uint64_t capacity) {
    std::uint8_t namespace_bytes[kStorageNamespaceBytes]{};
    StorageBootstrap bootstrap{};
    if (deserialize_storage_bootstrap(bootstrap_bytes, kStorageBootstrapBytes, &bootstrap) != StorageSerdeResult::Ok ||
        serialize_storage_namespace({capacity}, namespace_bytes, sizeof(namespace_bytes)) != StorageSerdeResult::Ok)
        return Error{ErrorCode::kProtocol, "invalid storage bootstrap record"};
    auto namespace_region =
        transport->register_memory(namespace_bytes, sizeof(namespace_bytes), MemoryAccess::LocalRead);
    if (!namespace_region.ok())
        return namespace_region.error();
    if (const auto completed = transport->write(0U, namespace_region.value(), bootstrap.namespace_response.address,
                                                bootstrap.namespace_response.remote_key, sizeof(namespace_bytes));
        !completed.ok())
        return completed.error();
    return bootstrap;
}

Result<std::vector<StorageSlot>> fetch_slots(Transport *transport, const StorageBootstrap &bootstrap) {
    if (transport == nullptr || bootstrap.slot_count == 0U ||
        bootstrap.slot_count > std::numeric_limits<std::size_t>::max() / sizeof(StorageSlot) ||
        bootstrap.slot_count > std::numeric_limits<std::uint32_t>::max() / sizeof(StorageSlot) ||
        bootstrap.slots.length < static_cast<std::uint64_t>(bootstrap.slot_count) * sizeof(StorageSlot))
        return Error{ErrorCode::kInvalidArgument, "invalid storage slot table"};
    std::vector<StorageSlot> slots(bootstrap.slot_count);
    auto slot_region =
        transport->register_memory(slots.data(), slots.size() * sizeof(StorageSlot), MemoryAccess::LocalWrite);
    if (!slot_region.ok())
        return slot_region.error();
    const std::size_t bytes = slots.size() * sizeof(StorageSlot);
    if (const auto fetched = transport->read(0U, slot_region.value(), bootstrap.slots.address,
                                             bootstrap.slots.remote_key, static_cast<std::uint32_t>(bytes));
        !fetched.ok())
        return fetched.error();
    for (const StorageSlot &slot : slots) {
        if (slot.reserved != 0U || slot.qp_index >= transport->qp_count() || slot.command.address == 0U ||
            slot.command.length < kStorageCommandBytes || slot.command.remote_key == 0U ||
            slot.completion.address == 0U || slot.completion.length < kStorageCompletionBytes ||
            slot.completion.remote_key == 0U)
            return Error{ErrorCode::kProtocol, "storage slot table contains an invalid slot"};
    }
    return slots;
}

Result<void> move_data(Transport *transport, std::size_t qp_index, const MemoryRegion *storage_region,
                       std::uint64_t offset, std::uint64_t length, const StorageMemory &remote, bool read) {
    if (length > std::numeric_limits<std::uint32_t>::max())
        return Error{ErrorCode::kProtocol, "storage transfer exceeds the transport length limit"};
    if (storage_region == nullptr)
        return Error{ErrorCode::kInvalidArgument, "storage namespace registration is missing"};
    const TransferRequest request{storage_region, offset, remote.address, remote.remote_key,
                                  static_cast<std::uint32_t>(length)};
    const std::array<TransferRequest, 1U> requests{request};
    const auto transferred =
        read ? transport->write_batch(qp_index, requests) : transport->read_batch(qp_index, requests);
    if (!transferred.ok())
        return transferred.error();
    return {};
}

template <typename Entry>
Result<void> move_batch_data(Transport *transport, std::size_t qp_index, const MemoryRegion *storage_region,
                             const std::vector<Entry> &entries, bool read) {
    std::vector<TransferRequest> requests;
    requests.reserve(entries.size());
    for (const Entry &entry : entries) {
        requests.push_back({storage_region, entry.offset, entry.data.address, entry.data.remote_key,
                            static_cast<std::uint32_t>(entry.length)});
    }
    const auto moved = read ? transport->write_batch(qp_index, requests) : transport->read_batch(qp_index, requests);
    if (!moved.ok())
        return moved.error();
    return {};
}

template <typename Command, typename Entry, typename DeserializeEntry>
Result<void> process_batch(Transport *transport, std::size_t qp_index, std::vector<unsigned char> *storage,
                           const MemoryRegion *storage_region, const Command &command, bool read,
                           DeserializeEntry deserialize_entry, StorageCompletion *completion) {
    const auto count = static_cast<std::size_t>(command.entry_count);
    std::vector<std::uint8_t> entry_bytes(count * kStorageBatchEntryBytes);
    auto entry_region = transport->register_memory(entry_bytes.data(), entry_bytes.size(), MemoryAccess::LocalWrite);
    if (!entry_region.ok())
        return entry_region.error();
    if (const auto fetched =
            transport->read(qp_index, entry_region.value(), command.entries.address, command.entries.remote_key,
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
    NDS_RETURN_IF_ERROR(move_batch_data(transport, qp_index, storage_region, entries, read));
    completion->bytes_transferred = total_length;
    return {};
}

template <typename Command>
Result<void> process_single(Transport *transport, std::size_t qp_index, std::vector<unsigned char> *storage,
                            const MemoryRegion *storage_region, const Command &command, bool read,
                            StorageCompletion *completion) {
    if (command.offset > storage->size() || command.length > storage->size() - command.offset ||
        command.length > std::numeric_limits<std::uint32_t>::max()) {
        completion->status = StorageStatus::RangeError;
        return {};
    }
    if (const auto moved =
            move_data(transport, qp_index, storage_region, command.offset, command.length, command.data, read);
        !moved.ok())
        return moved.error();
    completion->bytes_transferred = command.length;
    return {};
}

struct WorkerSlot {
    std::size_t slot_index{};
    std::array<std::uint8_t, kStorageCommandBytes> command_bytes{};
    std::array<std::uint8_t, kStorageCompletionBytes> completion_bytes{};
    MemoryRegion command_region;
    MemoryRegion completion_region;
};

struct Worker {
    std::size_t qp_index{};
    std::uint32_t command_count{};
    int cpu_id{-1};
    std::vector<WorkerSlot> slots;
};

#if defined(__linux__)
Result<std::vector<int>> allowed_cpus() {
    cpu_set_t cpu_set{};
    if (sched_getaffinity(0, sizeof(cpu_set), &cpu_set) != 0)
        return Error{ErrorCode::kRuntime,
                     "failed to read the server CPU affinity: " + std::string(std::strerror(errno))};
    std::vector<int> cpus;
    for (int cpu = 0; cpu < CPU_SETSIZE; ++cpu) {
        if (CPU_ISSET(cpu, &cpu_set))
            cpus.push_back(cpu);
    }
    if (cpus.empty())
        return Error{ErrorCode::kRuntime, "server CPU affinity has no available cores"};
    return cpus;
}

Result<void> pin_to_cpu(int cpu) {
    if (cpu < 0 || cpu >= CPU_SETSIZE)
        return Error{ErrorCode::kInvalidArgument, "invalid server worker CPU"};
    cpu_set_t cpu_set{};
    CPU_ZERO(&cpu_set);
    CPU_SET(cpu, &cpu_set);
    const int result = pthread_setaffinity_np(pthread_self(), sizeof(cpu_set), &cpu_set);
    if (result != 0)
        return Error{ErrorCode::kRuntime, "failed to pin server worker to CPU " + std::to_string(cpu) + ": " +
                                              std::string(std::strerror(result))};
    return {};
}
#else
Result<std::vector<int>> allowed_cpus() {
    return Error{ErrorCode::kRuntime, "dedicated server worker CPUs require Linux"};
}

Result<void> pin_to_cpu(int) {
    return Error{ErrorCode::kRuntime, "dedicated server worker CPUs require Linux"};
}
#endif

Result<void> run_worker(Transport *transport, std::vector<unsigned char> *storage,
                        const std::vector<StorageSlot> *slots, const MemoryRegion *storage_region, Worker *worker,
                        std::uint32_t timeout_ms) {
    if (worker == nullptr || worker->slots.empty())
        return Error{ErrorCode::kInvalidArgument, "storage worker has no command slots"};
    const std::size_t active_slots = std::min<std::size_t>(worker->command_count, worker->slots.size());
    for (std::size_t index = 0U; index < active_slots; ++index)
        NDS_RETURN_IF_ERROR(transport->post_receive(worker->qp_index, worker->slots[index].command_region));

    for (std::uint32_t command_index = 0U; command_index < worker->command_count; ++command_index) {
        WorkerSlot &slot = worker->slots[command_index % active_slots];
        NDS_RETURN_IF_ERROR(transport->wait_receive(worker->qp_index, timeout_ms));

        StorageOperation operation{};
        if (deserialize_storage_operation(slot.command_bytes.data(), slot.command_bytes.size(), &operation) !=
            StorageSerdeResult::Ok)
            return Error{ErrorCode::kProtocol, "invalid storage operation"};
        StorageCompletion completion{
            .command_id = 0U,
            .state = StorageCompletionState::Complete,
            .status = StorageStatus::Success,
            .bytes_transferred = 0U,
        };
        Result<void> processed;
        switch (operation) {
            case StorageOperation::Read: {
                StorageReadCommand command{};
                if (deserialize_storage_read(slot.command_bytes.data(), slot.command_bytes.size(), &command) !=
                    StorageSerdeResult::Ok)
                    return Error{ErrorCode::kProtocol, "invalid storage read command"};
                if (command.slot_index != slot.slot_index)
                    return Error{ErrorCode::kProtocol, "storage read arrived in the wrong slot"};
                completion.command_id = command.command_id;
                processed =
                    process_single(transport, worker->qp_index, storage, storage_region, command, true, &completion);
                break;
            }
            case StorageOperation::Write: {
                StorageWriteCommand command{};
                if (deserialize_storage_write(slot.command_bytes.data(), slot.command_bytes.size(), &command) !=
                    StorageSerdeResult::Ok)
                    return Error{ErrorCode::kProtocol, "invalid storage write command"};
                if (command.slot_index != slot.slot_index)
                    return Error{ErrorCode::kProtocol, "storage write arrived in the wrong slot"};
                completion.command_id = command.command_id;
                processed =
                    process_single(transport, worker->qp_index, storage, storage_region, command, false, &completion);
                break;
            }
            case StorageOperation::BatchRead: {
                StorageBatchReadCommand command{};
                if (deserialize_storage_batch_read(slot.command_bytes.data(), slot.command_bytes.size(), &command) !=
                    StorageSerdeResult::Ok)
                    return Error{ErrorCode::kProtocol, "invalid storage batch-read command"};
                if (command.slot_index != slot.slot_index)
                    return Error{ErrorCode::kProtocol, "storage batch-read arrived in the wrong slot"};
                completion.command_id = command.command_id;
                processed = process_batch<StorageBatchReadCommand, StorageBatchReadEntry>(
                    transport, worker->qp_index, storage, storage_region, command, true,
                    deserialize_storage_batch_read_entry, &completion);
                break;
            }
            case StorageOperation::BatchWrite: {
                StorageBatchWriteCommand command{};
                if (deserialize_storage_batch_write(slot.command_bytes.data(), slot.command_bytes.size(), &command) !=
                    StorageSerdeResult::Ok)
                    return Error{ErrorCode::kProtocol, "invalid storage batch-write command"};
                if (command.slot_index != slot.slot_index)
                    return Error{ErrorCode::kProtocol, "storage batch-write arrived in the wrong slot"};
                completion.command_id = command.command_id;
                processed = process_batch<StorageBatchWriteCommand, StorageBatchWriteEntry>(
                    transport, worker->qp_index, storage, storage_region, command, false,
                    deserialize_storage_batch_write_entry, &completion);
                break;
            }
        }
        if (!processed.ok())
            return processed.error();
        if (serialize_storage_completion(completion, slot.completion_bytes.data(), slot.completion_bytes.size()) !=
            StorageSerdeResult::Ok)
            return Error{ErrorCode::kProtocol, "invalid storage completion"};
        const StorageMemory &remote_completion = (*slots)[slot.slot_index].completion;
        if (const auto completed = transport->write(worker->qp_index, slot.completion_region, remote_completion.address,
                                                    remote_completion.remote_key, sizeof(slot.completion_bytes));
            !completed.ok())
            return completed.error();
        if (command_index + 1U < worker->command_count)
            NDS_RETURN_IF_ERROR(transport->post_receive(worker->qp_index, slot.command_region));
    }
    return {};
}

}  // namespace

Result<void> serve_commands(Transport *transport, std::vector<unsigned char> *storage, std::uint32_t command_count,
                            std::uint32_t timeout_ms) {
    if (transport == nullptr || storage == nullptr || command_count == 0U || transport->qp_count() == 0U)
        return Error{ErrorCode::kInvalidArgument, "transport, namespace, and QPs are required"};

    std::array<std::uint8_t, kStorageBootstrapBytes> bootstrap_bytes{};
    auto bootstrap_region =
        transport->register_memory(bootstrap_bytes.data(), bootstrap_bytes.size(), MemoryAccess::LocalWrite);
    if (!bootstrap_region.ok())
        return bootstrap_region.error();
    NDS_RETURN_IF_ERROR(transport->post_receive(0U, bootstrap_region.value()));
    if (const auto received = transport->wait_receive(0U, timeout_ms); !received.ok())
        return received.error();
    NDS_ASSIGN_OR_RETURN(StorageBootstrap bootstrap,
                         exchange_bootstrap(transport, bootstrap_bytes.data(), storage->size()));
    NDS_ASSIGN_OR_RETURN(std::vector<StorageSlot> slots, fetch_slots(transport, bootstrap));
    NDS_ASSIGN_OR_RETURN(MemoryRegion storage_region,
                         transport->register_memory(storage->data(), storage->size(), MemoryAccess::LocalWrite));
    std::vector<Worker> workers(transport->qp_count());
    for (std::size_t index = 0U; index < workers.size(); ++index) {
        workers[index].qp_index = index;
        workers[index].command_count =
            command_count / workers.size() + (index < command_count % workers.size() ? 1U : 0U);
        if (workers[index].command_count == 0U)
            continue;
        const std::size_t slot_count = static_cast<std::size_t>(std::count_if(
            slots.begin(), slots.end(), [index](const StorageSlot &slot) { return slot.qp_index == index; }));
        workers[index].slots.reserve(slot_count);
        for (std::size_t slot_index = 0U; slot_index < slots.size(); ++slot_index) {
            if (slots[slot_index].qp_index != index)
                continue;
            workers[index].slots.emplace_back();
            WorkerSlot &worker_slot = workers[index].slots.back();
            worker_slot.slot_index = slot_index;
            NDS_ASSIGN_OR_RETURN(
                worker_slot.command_region,
                transport->register_memory(worker_slot.command_bytes.data(), worker_slot.command_bytes.size(),
                                           MemoryAccess::LocalWrite));
            NDS_ASSIGN_OR_RETURN(
                worker_slot.completion_region,
                transport->register_memory(worker_slot.completion_bytes.data(), worker_slot.completion_bytes.size(),
                                           MemoryAccess::LocalRead));
        }
        if (workers[index].slots.empty())
            return Error{ErrorCode::kProtocol, "storage slot table does not cover a command QP"};
    }

    NDS_ASSIGN_OR_RETURN(std::vector<int> worker_cpus, allowed_cpus());
    std::size_t cpu_index{};
    for (Worker &worker : workers) {
        if (worker.command_count == 0U)
            continue;
        if (cpu_index == worker_cpus.size())
            return Error{ErrorCode::kRuntime, "server needs one allowed CPU per busy storage QP"};
        worker.cpu_id = worker_cpus[cpu_index++];
    }

    std::atomic<bool> failed{false};
    std::mutex error_mutex;
    std::optional<Error> first_error;
    std::vector<std::thread> threads;
    threads.reserve(workers.size());
    for (Worker &worker : workers) {
        if (worker.command_count == 0U)
            continue;
        threads.emplace_back([&, worker_ptr = &worker] {
            if (failed.load())
                return;
            if (const auto pinned = pin_to_cpu(worker_ptr->cpu_id); !pinned.ok()) {
                std::lock_guard<std::mutex> lock(error_mutex);
                if (!first_error.has_value())
                    first_error = pinned.error();
                failed.store(true);
                return;
            }
            const auto result = run_worker(transport, storage, &slots, &storage_region, worker_ptr, timeout_ms);
            if (!result.ok()) {
                std::lock_guard<std::mutex> lock(error_mutex);
                if (!first_error.has_value())
                    first_error = result.error();
                failed.store(true);
            }
        });
    }
    for (std::thread &thread : threads) thread.join();
    if (first_error.has_value())
        return first_error.value();
    return {};
}

}  // namespace nds::server
