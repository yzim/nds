#ifndef NDS_CLIENT_MEMORY_HH
#define NDS_CLIENT_MEMORY_HH

#include "nds/result.hh"

#include <cstddef>

namespace nds::client {

class Runtime;
struct EndpointTestAccess;

enum class MemoryLocation {
    Host,
    Device,
};

class MemoryBuffer {
public:
    MemoryBuffer() = default;
    ~MemoryBuffer();
    MemoryBuffer(const MemoryBuffer &) = delete;
    MemoryBuffer &operator=(const MemoryBuffer &) = delete;
    MemoryBuffer(MemoryBuffer &&other) noexcept;
    MemoryBuffer &operator=(MemoryBuffer &&other) noexcept;

    void *data() const noexcept;
    std::size_t size() const noexcept;
    MemoryLocation location() const noexcept;

private:
    friend class Memory;
    friend struct EndpointTestAccess;
    void reset() noexcept;

    Runtime *runtime_{};
    void *data_{};
    std::size_t size_{};
    MemoryLocation location_{MemoryLocation::Device};
};

/* Allocates and copies generic host or device buffers through one live runtime. */
class Memory {
public:
    Memory() = default;
    ~Memory() = default;
    Memory(const Memory &) = delete;
    Memory &operator=(const Memory &) = delete;

    Result<void> allocate(std::size_t size, MemoryBuffer *buffer);
    Result<void> allocate(std::size_t size, MemoryLocation location, MemoryBuffer *buffer);
    Result<void> copy_to(MemoryBuffer *buffer, const void *source, std::size_t size);
    Result<void> copy_from(void *destination, const MemoryBuffer &buffer, std::size_t size);

private:
    friend class Runtime;
    void attach(Runtime *runtime) noexcept;

    Runtime *runtime_{};
};

}  // namespace nds::client

#endif
