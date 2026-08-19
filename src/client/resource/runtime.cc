#include "runtime.hh"

#include <cstring>
#include <new>
#include <utility>

namespace nds::client {

Runtime::~Runtime() {
    reset();
}

Result<void> Runtime::open(const RuntimeConfig &config) {
    if (initialized_)
        return unexpected(ErrorCode::kInvalidArgument, "NPU runtime is already open");
    return initialize(config);
}

Result<void> Runtime::initialize(const RuntimeConfig &config) {
    const std::string hdc_type_argument = "--hdcType=" + std::to_string(config.hdc_type);
    nds_rt_proc_ext_param parameter{};
    nds_rt_net_service_open_args open_args{};

    if (!config.adopt_existing_context && (config.ascendcl_library.empty() || config.runtime_library.empty())) {
        return unexpected(ErrorCode::kInvalidArgument,
                          "NPU runtime requires explicit AscendCL and runtime library paths");
    }
    config_ = config;
    if (config_.ascendcl_library.empty())
        config_.ascendcl_library = "libascendcl.so";
    if (config_.runtime_library.empty())
        config_.runtime_library = "libruntime.so";
    parameter.param_info = hdc_type_argument.c_str();
    parameter.param_len = hdc_type_argument.size();
    open_args.ext_param_list = &parameter;
    open_args.ext_param_count = 1U;
    if (nds_acl_open(&acl_, config_.ascendcl_library.c_str()) != 0) {
        const std::string error = std::string("cannot load AscendCL: ") + nds_acl_error(&acl_);
        reset();
        return unexpected(ErrorCode::kRuntime, error);
    }
    if (!config_.adopt_existing_context) {
        if (const int result = acl_.init(nullptr); result != 0) {
            const std::string error = "aclInit failed: " + std::to_string(result);
            reset();
            return unexpected(ErrorCode::kRuntime, error);
        }
        acl_initialized_ = true;
        if (const int result = acl_.set_device(static_cast<std::int32_t>(config_.logical_device_id)); result != 0) {
            const std::string error = "aclrtSetDevice failed: " + std::to_string(result);
            reset();
            return unexpected(ErrorCode::kRuntime, error);
        }
    }
    if (nds_runtime_open(&runtime_, config_.runtime_library.c_str()) != 0) {
        const std::string error = std::string("cannot load CANN runtime: ") + nds_runtime_error(&runtime_);
        reset();
        return unexpected(ErrorCode::kRuntime, error);
    }
    if (const int result = runtime_.open_net_service(&open_args); result != 0) {
        const std::string error = "rtOpenNetService failed: " + std::to_string(result);
        reset();
        return unexpected(ErrorCode::kRuntime, error);
    }
    net_service_open_ = true;
    initialized_ = true;
    return {};
}

void Runtime::reset() noexcept {
    if (net_service_open_) {
        (void)runtime_.close_net_service();
        net_service_open_ = false;
    }
    nds_runtime_close(&runtime_);
    if (acl_initialized_) {
        (void)acl_.finalize();
        acl_initialized_ = false;
    }
    nds_acl_close(&acl_);
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
        return unexpected(ErrorCode::kInvalidArgument,
                          "device allocation requires an initialized runtime and nonzero size");
    }
    void *device_ptr = nullptr;
    const int result = acl_.malloc_device(&device_ptr, size, NDS_ACL_MEM_MALLOC_DIRECT_NPU);
    if (result != 0 || device_ptr == nullptr) {
        return unexpected(ErrorCode::kRuntime, "aclrtMalloc failed: " + std::to_string(result));
    }
    return device_ptr;
}

Result<void> Runtime::free_device_memory(void *device_ptr) {
    if (!initialized_ || device_ptr == nullptr) {
        return unexpected(ErrorCode::kInvalidArgument,
                          "device free requires an initialized runtime and allocation pointer");
    }
    const int result = acl_.free_device(device_ptr);
    if (result != 0) {
        return unexpected(ErrorCode::kRuntime, "aclrtFree failed: " + std::to_string(result));
    }
    return {};
}

Result<void> Runtime::copy_host_to_device(void *device_ptr, const void *host_ptr, std::size_t size) {
    const int result =
        (!initialized_ || device_ptr == nullptr || host_ptr == nullptr || size == 0U || acl_.memcpy == nullptr)
            ? -1
            : acl_.memcpy(device_ptr, size, host_ptr, size, NDS_ACL_MEMCPY_HOST_TO_DEVICE);
    if (result != 0) {
        return unexpected(ErrorCode::kRuntime, "aclrtMemcpy(host-to-device) failed: " + std::to_string(result));
    }
    return {};
}

Result<void> Runtime::copy_device_to_host(void *host_ptr, const void *device_ptr, std::size_t size) {
    const int result =
        (!initialized_ || device_ptr == nullptr || host_ptr == nullptr || size == 0U || acl_.memcpy == nullptr)
            ? -1
            : acl_.memcpy(host_ptr, size, device_ptr, size, NDS_ACL_MEMCPY_DEVICE_TO_HOST);
    if (result != 0) {
        return unexpected(ErrorCode::kRuntime, "aclrtMemcpy(device-to-host) failed: " + std::to_string(result));
    }
    return {};
}

nds_acl_api &Runtime::acl_api() noexcept {
    return acl_;
}

nds_runtime_api &Runtime::runtime_api() noexcept {
    return runtime_;
}

MemoryBuffer::~MemoryBuffer() {
    reset();
}

MemoryBuffer::MemoryBuffer(MemoryBuffer &&other) noexcept
    : runtime_(std::exchange(other.runtime_, nullptr)),
      data_(std::exchange(other.data_, nullptr)),
      size_(std::exchange(other.size_, 0U)),
      location_(other.location_) {}

MemoryBuffer &MemoryBuffer::operator=(MemoryBuffer &&other) noexcept {
    if (this != &other) {
        reset();
        runtime_ = std::exchange(other.runtime_, nullptr);
        data_ = std::exchange(other.data_, nullptr);
        size_ = std::exchange(other.size_, 0U);
        location_ = other.location_;
    }
    return *this;
}

void MemoryBuffer::reset() noexcept {
    if (data_ != nullptr) {
        if (location_ == MemoryLocation::Device && runtime_ != nullptr)
            (void)runtime_->free_device_memory(data_);
        if (location_ == MemoryLocation::Host)
            delete[] static_cast<std::byte *>(data_);
    }
    runtime_ = nullptr;
    data_ = nullptr;
    size_ = 0U;
    location_ = MemoryLocation::Device;
}

void *MemoryBuffer::data() const noexcept {
    return data_;
}

std::size_t MemoryBuffer::size() const noexcept {
    return size_;
}

MemoryLocation MemoryBuffer::location() const noexcept {
    return location_;
}

Result<MemoryBuffer> Runtime::allocate(std::size_t size) {
    return allocate(size, MemoryLocation::Device);
}

Result<MemoryBuffer> Runtime::allocate(std::size_t size, MemoryLocation location) {
    if (!initialized_ || size == 0U)
        return unexpected(ErrorCode::kInvalidArgument, "memory allocation requires an open runtime and nonzero size");
    MemoryBuffer buffer;
    if (location == MemoryLocation::Device) {
        auto allocated = allocate_device_memory(size);
        if (!allocated)
            return unexpected(allocated.error());
        buffer.data_ = *allocated;
        buffer.runtime_ = this;
    } else {
        buffer.data_ = new (std::nothrow) std::byte[size];
        if (buffer.data_ == nullptr)
            return unexpected(ErrorCode::kRuntime, "host memory allocation failed");
    }
    buffer.size_ = size;
    buffer.location_ = location;
    return buffer;
}

Result<void> Runtime::copy_to(MemoryBuffer *buffer, const void *source, std::size_t size) {
    if (!initialized_ || buffer == nullptr || buffer->data_ == nullptr || source == nullptr || size > buffer->size_ ||
        (buffer->location_ == MemoryLocation::Device && buffer->runtime_ != this)) {
        return unexpected(ErrorCode::kInvalidArgument, "memory copy requires a runtime buffer and valid source");
    }
    if (buffer->location_ == MemoryLocation::Host) {
        std::memcpy(buffer->data_, source, size);
        return {};
    }
    return copy_host_to_device(buffer->data_, source, size);
}

Result<void> Runtime::copy_from(void *destination, const MemoryBuffer &buffer, std::size_t size) {
    if (!initialized_ || destination == nullptr || buffer.data_ == nullptr || size > buffer.size_ ||
        (buffer.location_ == MemoryLocation::Device && buffer.runtime_ != this)) {
        return unexpected(ErrorCode::kInvalidArgument, "memory copy requires a runtime buffer and valid destination");
    }
    if (buffer.location_ == MemoryLocation::Host) {
        std::memcpy(destination, buffer.data_, size);
        return {};
    }
    return copy_device_to_host(destination, buffer.data_, size);
}

}  // namespace nds::client
