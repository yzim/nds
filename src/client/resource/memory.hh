#ifndef NDS_CLIENT_MEMORY_HH
#define NDS_CLIENT_MEMORY_HH

#include "nds/npu_ra_context.hh"
#include "nds/npu_ra_qp.hh"
#include "nds/result.hh"

#include <cstddef>
#include <cstdint>

namespace nds::client {

struct LocalAddress {
    std::uint64_t address{};
    std::uint32_t key{};
};

struct RemoteAddress {
    std::uint64_t address{};
    std::uint32_t key{};
};

class DeviceBuffer {
public:
    DeviceBuffer() = default;
    ~DeviceBuffer();
    DeviceBuffer(const DeviceBuffer &) = delete;
    DeviceBuffer &operator=(const DeviceBuffer &) = delete;

    void *data() const noexcept;
    std::size_t size() const noexcept;

private:
    friend class Memory;
    NpuRaContext *context_{};
    void *data_{};
    std::size_t size_{};
};

class RegisteredRegion {
public:
    RegisteredRegion() = default;
    ~RegisteredRegion();
    RegisteredRegion(const RegisteredRegion &) = delete;
    RegisteredRegion &operator=(const RegisteredRegion &) = delete;

    LocalAddress local_address() const noexcept;
    RemoteAddress remote_address() const noexcept;
    std::uint64_t length() const noexcept;
    bool belongs_to(const NpuRaQp *qp) const noexcept;

private:
    friend class Memory;
    NpuRaQp *qp_{};
    nds_ra_mr_info info_{};
    void *handle_{};
};

/* Allocates, copies, and registers memory through one live NPU runtime. */
class Memory {
public:
    Memory() = default;
    ~Memory() = default;
    Memory(const Memory &) = delete;
    Memory &operator=(const Memory &) = delete;

    Result<void> allocate(std::size_t size, DeviceBuffer *buffer);
    Result<void> register_memory(NpuRaQp *qp, DeviceBuffer *buffer, RegisteredRegion *region);
    Result<void> copy_to_device(DeviceBuffer *buffer, const void *source, std::size_t size);
    Result<void> copy_from_device(void *destination, const DeviceBuffer &buffer, std::size_t size);

private:
    friend class NpuRuntime;
    void attach(NpuRaContext *context) noexcept;

    NpuRaContext *context_{};
};

}  // namespace nds::client

#endif
