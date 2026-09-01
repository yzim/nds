#include "runtime.hh"

#include <cstring>
#include <cstdlib>
#include <limits>
#include <new>
#include <utility>

namespace nds::client {
namespace {

constexpr std::size_t kHostPageSize = 4096U;

Result<std::size_t> page_rounded_size(std::size_t size) {
    if (size > std::numeric_limits<std::size_t>::max() - (kHostPageSize - 1U))
        return Error{ErrorCode::kInvalidArgument, "host-pinned allocation size is too large"};
    return (size + kHostPageSize - 1U) & ~(kHostPageSize - 1U);
}

}  // namespace

Runtime::~Runtime() {
    reset();
}

Result<void> Runtime::open(const RuntimeConfig &config) {
    if (initialized_)
        return Error{ErrorCode::kInvalidArgument, "NPU runtime is already open"};
    return initialize(config);
}

Result<void> Runtime::initialize(const RuntimeConfig &config) {
    const std::string hdc_type_argument = "--hdcType=" + std::to_string(config.hdc_type);
    Libruntime::ProcExtParam parameter{};
    Libruntime::NetServiceOpenArgs open_args{};

    config_ = config;
    parameter.param_info = hdc_type_argument.c_str();
    parameter.param_len = hdc_type_argument.size();
    open_args.ext_param_list = &parameter;
    open_args.ext_param_count = 1U;
    if (!config_.adopt_existing_context) {
        if (const int result = aclInit(nullptr); result != ACL_SUCCESS) {
            const std::string error = "aclInit failed: " + std::to_string(result);
            reset();
            return Error{ErrorCode::kRuntime, error};
        }
        acl_initialized_ = true;
        if (const int result = aclrtSetDevice(static_cast<std::int32_t>(config_.logical_device_id));
            result != ACL_SUCCESS) {
            const std::string error = "aclrtSetDevice failed: " + std::to_string(result);
            reset();
            return Error{ErrorCode::kRuntime, error};
        }
    }
    Result<Libruntime> libruntime_result = Libruntime::open();
    if (!libruntime_result.ok()) {
        reset();
        return Error{libruntime_result.error()};
    }
    libruntime_ = std::move(libruntime_result).value();
    Result<Libdsmi> libdsmi_result = Libdsmi::open();
    if (!libdsmi_result.ok()) {
        reset();
        return Error{libdsmi_result.error()};
    }
    libdsmi_ = std::move(libdsmi_result).value();
    if (const int result = libruntime_.open_net_service(&open_args); result != 0) {
        const std::string error = "rtOpenNetService failed: " + std::to_string(result);
        reset();
        return Error{ErrorCode::kRuntime, error};
    }
    net_service_open_ = true;
    initialized_ = true;
    return {};
}

void Runtime::reset() noexcept {
    if (net_service_open_) {
        (void)libruntime_.close_net_service();
        net_service_open_ = false;
    }
    if (acl_initialized_) {
        (void)aclFinalize();
        acl_initialized_ = false;
    }
    initialized_ = false;
}

const RuntimeConfig &Runtime::config() const noexcept {
    return config_;
}

bool Runtime::initialized() const noexcept {
    return initialized_;
}

Result<void *> Runtime::allocate_device_memory(std::size_t size) {
    if (!initialized_ || size == 0U) {
        return Error{ErrorCode::kInvalidArgument, "device allocation requires an initialized runtime and nonzero size"};
    }
    void *device_ptr = nullptr;
    const auto policy = static_cast<aclrtMemMallocPolicy>(ACL_MEM_MALLOC_HUGE_FIRST | ACL_MEM_TYPE_HIGH_BAND_WIDTH);
    const int result = aclrtMalloc(&device_ptr, size, policy);
    if (result != ACL_SUCCESS || device_ptr == nullptr) {
        return Error{ErrorCode::kRuntime, "aclrtMalloc failed: " + std::to_string(result)};
    }
    return device_ptr;
}

Result<void> Runtime::free_device_memory(void *device_ptr) {
    if (!initialized_ || device_ptr == nullptr) {
        return Error{ErrorCode::kInvalidArgument, "device free requires an initialized runtime and allocation pointer"};
    }
    const int result = aclrtFree(device_ptr);
    if (result != ACL_SUCCESS) {
        return Error{ErrorCode::kRuntime, "aclrtFree failed: " + std::to_string(result)};
    }
    return {};
}

Result<HostPinnedAllocation> Runtime::allocate_host_pinned_memory(std::size_t size) {
    if (!initialized_ || size == 0U)
        return Error{ErrorCode::kInvalidArgument,
                     "host-pinned allocation requires an initialized runtime and nonzero size"};
    const auto rounded_size = page_rounded_size(size);
    if (!rounded_size.ok())
        return Error{rounded_size.error()};
    void *host_ptr = nullptr;
    if (posix_memalign(&host_ptr, kHostPageSize, rounded_size.value()) != 0 || host_ptr == nullptr)
        return Error{ErrorCode::kRuntime, "host memory allocation failed"};
    void *device_ptr = nullptr;
    const int result = aclrtHostRegister(host_ptr, rounded_size.value(), ACL_HOST_REGISTER_MAPPED, &device_ptr);
    if (result != ACL_SUCCESS || device_ptr == nullptr) {
        std::free(host_ptr);
        return Error{ErrorCode::kRuntime, "aclrtHostRegister failed: " + std::to_string(result)};
    }
    return HostPinnedAllocation{host_ptr, device_ptr};
}

Result<void> Runtime::free_host_pinned_memory(void *host_ptr) {
    if (!initialized_ || host_ptr == nullptr)
        return Error{ErrorCode::kInvalidArgument,
                     "host-pinned free requires an initialized runtime and allocation pointer"};
    const int result = aclrtHostUnregister(host_ptr);
    if (result != ACL_SUCCESS)
        return Error{ErrorCode::kRuntime, "aclrtHostUnregister failed: " + std::to_string(result)};
    std::free(host_ptr);
    return {};
}

Result<void> Runtime::copy_host_to_device(void *device_ptr, const void *host_ptr, std::size_t size) {
    const int result = (!initialized_ || device_ptr == nullptr || host_ptr == nullptr || size == 0U)
                           ? -1
                           : aclrtMemcpy(device_ptr, size, host_ptr, size, ACL_MEMCPY_HOST_TO_DEVICE);
    if (result != ACL_SUCCESS) {
        return Error{ErrorCode::kRuntime, "aclrtMemcpy(host-to-device) failed: " + std::to_string(result)};
    }
    return {};
}

Result<void> Runtime::copy_device_to_host(void *host_ptr, const void *device_ptr, std::size_t size) {
    const int result = (!initialized_ || device_ptr == nullptr || host_ptr == nullptr || size == 0U)
                           ? -1
                           : aclrtMemcpy(host_ptr, size, device_ptr, size, ACL_MEMCPY_DEVICE_TO_HOST);
    if (result != ACL_SUCCESS) {
        return Error{ErrorCode::kRuntime, "aclrtMemcpy(device-to-host) failed: " + std::to_string(result)};
    }
    return {};
}

Libruntime &Runtime::libruntime() noexcept {
    return libruntime_;
}

Libdsmi &Runtime::libdsmi() noexcept {
    return libdsmi_;
}

MemoryBuffer::~MemoryBuffer() {
    reset();
}

MemoryBuffer::MemoryBuffer(MemoryBuffer &&other) noexcept
    : runtime_(std::exchange(other.runtime_, nullptr)),
      data_(std::exchange(other.data_, nullptr)),
      rdma_data_(std::exchange(other.rdma_data_, nullptr)),
      size_(std::exchange(other.size_, 0U)),
      location_(other.location_) {}

MemoryBuffer &MemoryBuffer::operator=(MemoryBuffer &&other) noexcept {
    if (this != &other) {
        reset();
        runtime_ = std::exchange(other.runtime_, nullptr);
        data_ = std::exchange(other.data_, nullptr);
        rdma_data_ = std::exchange(other.rdma_data_, nullptr);
        size_ = std::exchange(other.size_, 0U);
        location_ = other.location_;
    }
    return *this;
}

void MemoryBuffer::reset() noexcept {
    if (data_ != nullptr) {
        if (location_ == MemoryLocation::Device && runtime_ != nullptr)
            (void)runtime_->free_device_memory(data_);
        if (location_ == MemoryLocation::HostPinned && runtime_ != nullptr)
            (void)runtime_->free_host_pinned_memory(data_);
        if (location_ == MemoryLocation::Host)
            delete[] static_cast<std::byte *>(data_);
    }
    runtime_ = nullptr;
    data_ = nullptr;
    rdma_data_ = nullptr;
    size_ = 0U;
    location_ = MemoryLocation::Device;
}

void *MemoryBuffer::data() const noexcept {
    return data_;
}

void *MemoryBuffer::rdma_data() const noexcept {
    return rdma_data_;
}

std::size_t MemoryBuffer::size() const noexcept {
    return size_;
}

MemoryLocation MemoryBuffer::location() const noexcept {
    return location_;
}

Result<MemoryBuffer> Runtime::allocate(std::size_t size, MemoryLocation location) {
    if (!initialized_ || size == 0U)
        return Error{ErrorCode::kInvalidArgument, "memory allocation requires an open runtime and nonzero size"};
    MemoryBuffer buffer;
    if (location == MemoryLocation::Device) {
        auto allocated = allocate_device_memory(size);
        if (!allocated.ok())
            return Error{allocated.error()};
        buffer.data_ = allocated.value();
        buffer.rdma_data_ = allocated.value();
        buffer.runtime_ = this;
    } else if (location == MemoryLocation::HostPinned) {
        auto allocated = allocate_host_pinned_memory(size);
        if (!allocated.ok())
            return Error{allocated.error()};
        buffer.data_ = allocated.value().host_address;
        buffer.rdma_data_ = allocated.value().device_address;
        buffer.runtime_ = this;
    } else {
        buffer.data_ = new (std::nothrow) std::byte[size];
        if (buffer.data_ == nullptr)
            return Error{ErrorCode::kRuntime, "host memory allocation failed"};
        buffer.rdma_data_ = buffer.data_;
    }
    buffer.size_ = size;
    buffer.location_ = location;
    return buffer;
}

Result<void> Runtime::copy_to(MemoryBuffer *buffer, const void *source, std::size_t size) {
    if (!initialized_ || buffer == nullptr || buffer->data_ == nullptr || source == nullptr || size > buffer->size_ ||
        (buffer->location_ != MemoryLocation::Host && buffer->runtime_ != this)) {
        return Error{ErrorCode::kInvalidArgument, "memory copy requires a runtime buffer and valid source"};
    }
    if (buffer->location_ == MemoryLocation::Host || buffer->location_ == MemoryLocation::HostPinned) {
        std::memcpy(buffer->data_, source, size);
        return {};
    }
    return copy_host_to_device(buffer->data_, source, size);
}

Result<void> Runtime::copy_from(void *destination, const MemoryBuffer &buffer, std::size_t size) {
    if (!initialized_ || destination == nullptr || buffer.data_ == nullptr || size > buffer.size_ ||
        (buffer.location_ != MemoryLocation::Host && buffer.runtime_ != this)) {
        return Error{ErrorCode::kInvalidArgument, "memory copy requires a runtime buffer and valid destination"};
    }
    if (buffer.location_ == MemoryLocation::Host || buffer.location_ == MemoryLocation::HostPinned) {
        std::memcpy(destination, buffer.data_, size);
        return {};
    }
    return copy_device_to_host(destination, buffer.data_, size);
}

}  // namespace nds::client
