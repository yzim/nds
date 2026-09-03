#ifndef NDS_CLIENT_STORAGE_HH
#define NDS_CLIENT_STORAGE_HH

#include "runtime.hh"
#include "transport.hh"
#include "storage_protocol.hh"
#include "backend_storage.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace nds::client {

struct StorageClientTestAccess;

/* Host implementation of the NDS storage API. Device implementations use the same semantics. */
class StorageClient {
public:
    ~StorageClient();

    Result<void> open(Runtime *runtime, Transport *transport);
    Result<MemoryRegion> register_memory(const MemoryBuffer &buffer, MemoryAccess access);
    Result<void> complete_bootstrap(std::uint32_t timeout_ms = 5000U);

    std::uint64_t capacity() const noexcept;
    std::size_t slot_count() const noexcept;
    const NdsStorageDescriptor &descriptor() const noexcept;
    const NdsStorageBootstrapDescriptor &bootstrap_descriptor() const noexcept;
    Result<std::uint32_t> allocate_slot();
    Result<std::uint32_t> allocate_slot(std::uint32_t queue_index);
    Result<void> release_slot(std::uint32_t slot_id);

private:
    friend struct StorageClientTestAccess;
    Result<void> clear_state(std::uint32_t slot_index);
    std::uint64_t allocate_command_id() noexcept;
    Result<void> prepare_bootstrap();
    Result<std::uint64_t> observe_namespace(std::uint32_t timeout_ms);
    Runtime *runtime_{};
    Transport *transport_{};
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
    MemoryBuffer storage_states_buffer_;
    MemoryBuffer slot_table_buffer_;
    MemoryRegion slot_table_region_;
    std::vector<bool> allocated_slots_;
    std::uint64_t capacity_{};
    std::uint64_t next_command_id_{};
    NdsStorageDescriptor storage_descriptor_{};
    NdsStorageBootstrapDescriptor bootstrap_descriptor_{};
    std::size_t next_slot_{};
    bool opened_{};
};

}  // namespace nds::client

#endif
