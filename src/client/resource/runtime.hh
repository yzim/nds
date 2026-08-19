#ifndef NDS_CLIENT_RUNTIME_HH
#define NDS_CLIENT_RUNTIME_HH

#include "memory.hh"
#include "nds/acl_loader.h"
#include "nds/result.hh"
#include "nds/runtime_loader.h"

#include <cstddef>
#include <cstdint>
#include <string>

namespace nds::client {

struct RuntimeConfig {
    std::string ascendcl_library;
    std::string runtime_library;
    std::uint32_t logical_device_id{};
    std::int32_t hdc_type{NDS_RUNTIME_HDC_SERVICE_TYPE_RDMA_V2};
};

/* Owns process-local AscendCL/CANN lifecycle and its memory service. */
class Runtime {
public:
    Runtime() = default;
    ~Runtime();
    Runtime(const Runtime &) = delete;
    Runtime &operator=(const Runtime &) = delete;

    Result<void> open(const RuntimeConfig &config);

    Memory *memory() noexcept;
    const RuntimeConfig &config() const noexcept;
    bool initialized() const noexcept;

    Result<void> allocate_device_memory(std::size_t size, void **device_ptr);
    Result<void> free_device_memory(void *device_ptr);
    Result<void> copy_host_to_device(void *device_ptr, const void *host_ptr, std::size_t size);
    Result<void> copy_device_to_host(void *host_ptr, const void *device_ptr, std::size_t size);
    nds_acl_api &acl_api() noexcept;
    nds_runtime_api &runtime_api() noexcept;
    const std::string &error() const noexcept;

private:
    bool initialize(const RuntimeConfig &config);
    void reset() noexcept;
    void set_error(std::string message);

    RuntimeConfig config_{};
    nds_acl_api acl_{};
    nds_runtime_api runtime_{};
    bool acl_initialized_{false};
    bool net_service_open_{false};
    bool initialized_{false};
    std::string error_;
    Memory memory_;
};

}  // namespace nds::client

#endif
