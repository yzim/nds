#ifndef NDS_CLIENT_STORAGE_HH
#define NDS_CLIENT_STORAGE_HH

#include "memory.hh"
#include "runtime.hh"
#include "transport.hh"

#include <cstddef>
#include <cstdint>

namespace nds::client {

/* Host implementation of the NDS storage API. Device implementations use the same semantics. */
class StorageClient {
public:
    Result<void> open(Runtime *runtime, Transport *transport);
    Result<void> read(std::uint64_t offset, MemoryBuffer *data, std::uint32_t length);
    Result<void> write(std::uint64_t offset, MemoryBuffer *data, std::uint32_t length);

    std::uint64_t capacity() const noexcept;

private:
    Result<void> execute(std::uint16_t operation, std::uint64_t offset, MemoryBuffer *data, std::uint32_t length);
    Result<std::uint64_t> exchange_bootstrap();
    Runtime *runtime_{};
    Transport *transport_{};
    MemoryBuffer command_buffer_;
    MemoryBuffer completion_buffer_;
    MemoryRegion command_region_;
    MemoryRegion completion_region_;
    std::uint64_t capacity_{};
    std::uint64_t next_request_id_{};
    bool opened_{};
    bool request_submitted_{};
};

}  // namespace nds::client

#endif
