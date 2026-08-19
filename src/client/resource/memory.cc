#include "memory.hh"

#include "runtime.hh"

#include <cstring>
#include <new>
#include <utility>

namespace nds::client {

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

Result<void> Memory::allocate(std::size_t size, MemoryBuffer *buffer) {
    return allocate(size, MemoryLocation::Device, buffer);
}

Result<void> Memory::allocate(std::size_t size, MemoryLocation location, MemoryBuffer *buffer) {
    if (runtime_ == nullptr || !runtime_->initialized() || buffer == nullptr || buffer->data_ != nullptr || size == 0U)
        return unexpected(ErrorCode::kInvalidArgument, "memory allocation requires an open runtime and empty buffer");
    if (location == MemoryLocation::Device) {
        if (const auto allocated = runtime_->allocate_device_memory(size, &buffer->data_); !allocated)
            return unexpected(allocated.error());
        buffer->runtime_ = runtime_;
    } else {
        buffer->data_ = new (std::nothrow) std::byte[size];
        if (buffer->data_ == nullptr)
            return unexpected(ErrorCode::kRuntime, "host memory allocation failed");
    }
    buffer->size_ = size;
    buffer->location_ = location;
    return {};
}

Result<void> Memory::copy_to(MemoryBuffer *buffer, const void *source, std::size_t size) {
    if (runtime_ == nullptr || buffer == nullptr || buffer->data_ == nullptr || source == nullptr ||
        size > buffer->size_ || (buffer->location_ == MemoryLocation::Device && buffer->runtime_ != runtime_)) {
        return unexpected(ErrorCode::kInvalidArgument, "memory copy requires a runtime buffer and valid source");
    }
    if (buffer->location_ == MemoryLocation::Host) {
        std::memcpy(buffer->data_, source, size);
        return {};
    }
    return runtime_->copy_host_to_device(buffer->data_, source, size);
}

Result<void> Memory::copy_from(void *destination, const MemoryBuffer &buffer, std::size_t size) {
    if (runtime_ == nullptr || destination == nullptr || buffer.data_ == nullptr || size > buffer.size_ ||
        (buffer.location_ == MemoryLocation::Device && buffer.runtime_ != runtime_)) {
        return unexpected(ErrorCode::kInvalidArgument, "memory copy requires a runtime buffer and valid destination");
    }
    if (buffer.location_ == MemoryLocation::Host) {
        std::memcpy(destination, buffer.data_, size);
        return {};
    }
    return runtime_->copy_device_to_host(destination, buffer.data_, size);
}

void Memory::attach(Runtime *runtime) noexcept {
    runtime_ = runtime;
}

}  // namespace nds::client
