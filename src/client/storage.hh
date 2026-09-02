#ifndef NDS_CLIENT_STORAGE_HH
#define NDS_CLIENT_STORAGE_HH

#include "runtime.hh"
#include "transport.hh"
#include "storage_protocol.hh"
#include "backend_storage.h"
#include "backends/launcher.hh"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace nds::client {

struct StorageClientTestAccess;

struct StorageIo {
    std::uint64_t offset{};
    MemoryBuffer *data{};
    std::uint32_t length{};
};

struct StorageCompletionHandle {
    std::uint64_t command_id{};
    std::uint32_t slot_index{};
};

/* Host implementation of the NDS storage API. Device implementations use the same semantics. */
class StorageClient {
public:
    ~StorageClient();

    Result<void> open(Runtime *runtime, Transport *transport);
    Result<StorageCompletionHandle> read(std::uint64_t offset, MemoryBuffer *data, std::uint32_t length);
    Result<StorageCompletionHandle> write(std::uint64_t offset, MemoryBuffer *data, std::uint32_t length);
    Result<StorageCompletionHandle> read_batch(std::span<const StorageIo> requests);
    Result<StorageCompletionHandle> write_batch(std::span<const StorageIo> requests);
    Result<void> wait(StorageCompletionHandle handle, std::uint32_t timeout_ms);

    std::uint64_t capacity() const noexcept;
    std::size_t slot_count() const noexcept;

private:
    friend struct StorageClientTestAccess;
    Result<void> validate_io(const StorageIo &command) const;
    Result<std::uint32_t> acquire_slot();
    Result<StorageCompletion> observe_completion(std::uint32_t slot_index, std::uint64_t command_id,
                                                 std::uint64_t expected_bytes, std::uint32_t timeout_ms);
    Result<void> execute_storage_read(const StorageReadCommand &command);
    Result<void> execute_storage_write(const StorageWriteCommand &command);
    Result<void> execute_storage_batch_read(const StorageBatchReadCommand &command);
    Result<void> execute_storage_batch_write(const StorageBatchWriteCommand &command);
    std::uint64_t allocate_command_id() noexcept;
    Result<std::uint64_t> exchange_bootstrap();
    Runtime *runtime_{};
    Transport *transport_{};
    std::unique_ptr<Launcher> launcher_;
    MemoryBuffer bootstrap_buffer_;
    MemoryRegion bootstrap_region_;
    MemoryBuffer namespace_buffer_;
    MemoryRegion namespace_region_;
    struct SlotResources {
        std::size_t command_offset{};
        std::size_t completion_offset{};
        std::uint32_t qp_index{};
    };
    MemoryBuffer command_buffer_;
    MemoryRegion command_region_;
    MemoryBuffer completion_buffer_;
    MemoryRegion completion_region_;
    std::vector<SlotResources> slots_;
    MemoryBuffer slot_descriptors_buffer_;
    MemoryBuffer slot_table_buffer_;
    MemoryRegion slot_table_region_;
    std::uint64_t capacity_{};
    std::uint64_t next_command_id_{};
    struct PendingRequest {
        std::uint64_t command_id{};
        std::uint64_t expected_bytes{};
        std::vector<MemoryRegion> data_regions;
        MemoryBuffer descriptor_buffer;
        MemoryRegion descriptor_region;
    };
    std::vector<std::optional<PendingRequest>> pending_;
    std::size_t next_slot_{};
    NdsStorageDescriptor storage_descriptor_{};
    bool opened_{};
};

}  // namespace nds::client

#endif
