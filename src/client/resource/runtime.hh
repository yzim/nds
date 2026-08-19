#ifndef NDS_CLIENT_RUNTIME_HH
#define NDS_CLIENT_RUNTIME_HH

#include "nds/acl_loader.h"
#include "nds/result.hh"
#include "nds/runtime_loader.h"

#include <cstddef>
#include <cstdint>
#include <string>

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
    friend class Runtime;
    friend struct EndpointTestAccess;
    void reset() noexcept;

    Runtime *runtime_{};
    void *data_{};
    std::size_t size_{};
    MemoryLocation location_{MemoryLocation::Device};
};

struct RuntimeConfig {
    std::string ascendcl_library;
    std::string runtime_library;
    std::uint32_t logical_device_id{};
    std::int32_t hdc_type{NDS_RUNTIME_HDC_SERVICE_TYPE_RDMA_V2};
    bool adopt_existing_context{};
};

/* Owns process-local AscendCL/CANN lifecycle and device/host memory operations. */
class Runtime {
public:
    Runtime() = default;
    ~Runtime();
    Runtime(const Runtime &) = delete;
    Runtime &operator=(const Runtime &) = delete;

    Result<void> open(const RuntimeConfig &config);

    const RuntimeConfig &config() const noexcept;
    bool initialized() const noexcept;

    Result<MemoryBuffer> allocate(std::size_t size);
    Result<MemoryBuffer> allocate(std::size_t size, MemoryLocation location);
    Result<void> copy_to(MemoryBuffer *buffer, const void *source, std::size_t size);
    Result<void> copy_from(void *destination, const MemoryBuffer &buffer, std::size_t size);

    Result<void *> allocate_device_memory(std::size_t size);
    Result<void> free_device_memory(void *device_ptr);
    Result<void> copy_host_to_device(void *device_ptr, const void *host_ptr, std::size_t size);
    Result<void> copy_device_to_host(void *host_ptr, const void *device_ptr, std::size_t size);
    nds_acl_api &acl_api() noexcept;
    nds_runtime_api &runtime_api() noexcept;

private:
    Result<void> initialize(const RuntimeConfig &config);
    void reset() noexcept;

    RuntimeConfig config_{};
    nds_acl_api acl_{};
    nds_runtime_api runtime_{};
    bool acl_initialized_{false};
    bool net_service_open_{false};
    bool initialized_{false};
};

}  // namespace nds::client

#endif
