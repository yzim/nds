#include "memory.hh"

namespace nds::client {

DeviceBuffer::~DeviceBuffer() {
    if (context_ != nullptr && data_ != nullptr)
        (void)context_->free_device_memory(data_);
}

void *DeviceBuffer::data() const noexcept {
    return data_;
}
std::size_t DeviceBuffer::size() const noexcept {
    return size_;
}

RegisteredRegion::~RegisteredRegion() {
    if (qp_ != nullptr && handle_ != nullptr)
        (void)qp_->deregister_memory(handle_);
}

LocalAddress RegisteredRegion::local_address() const noexcept {
    return {reinterpret_cast<std::uint64_t>(info_.address), info_.local_key};
}

RemoteAddress RegisteredRegion::remote_address() const noexcept {
    return {reinterpret_cast<std::uint64_t>(info_.address), info_.remote_key};
}

std::uint64_t RegisteredRegion::length() const noexcept {
    return info_.size;
}
bool RegisteredRegion::belongs_to(const NpuRaQp *qp) const noexcept {
    return qp_ == qp && handle_ != nullptr;
}

Result<void> Memory::allocate(std::size_t size, DeviceBuffer *buffer) {
    if (context_ == nullptr || buffer == nullptr || buffer->context_ != nullptr || buffer->data_ != nullptr ||
        size == 0U)
        return unexpected(ErrorCode::kInvalidArgument, "memory allocation requires an open runtime and empty buffer");
    if (!context_->allocate_device_memory(size, &buffer->data_))
        return unexpected(ErrorCode::kRuntime,
                          context_->error().empty() ? "invalid device allocation" : context_->error());
    buffer->context_ = context_;
    buffer->size_ = size;
    return {};
}

Result<void> Memory::register_memory(NpuRaQp *qp, DeviceBuffer *buffer, RegisteredRegion *region) {
    if (context_ == nullptr || qp == nullptr || buffer == nullptr || buffer->context_ != context_ ||
        buffer->data_ == nullptr || region == nullptr || region->qp_ != nullptr || region->handle_ != nullptr) {
        return unexpected(ErrorCode::kInvalidArgument,
                          "memory registration requires a runtime buffer and empty region");
    }
    if (!qp->register_memory(buffer->data_, buffer->size_, NDS_RA_ACCESS_DIRECT_NPU, &region->info_, &region->handle_))
        return unexpected(ErrorCode::kRa, qp->error().empty() ? "invalid memory registration" : qp->error());
    region->qp_ = qp;
    return {};
}

Result<void> Memory::copy_to_device(DeviceBuffer *buffer, const void *source, std::size_t size) {
    if (context_ == nullptr || buffer == nullptr || buffer->context_ != context_ || source == nullptr ||
        size > buffer->size_)
        return unexpected(ErrorCode::kInvalidArgument, "host-to-device copy requires a runtime buffer");
    if (!context_->copy_host_to_device(buffer->data_, source, size))
        return unexpected(ErrorCode::kRuntime,
                          context_->error().empty() ? "invalid host-to-device copy" : context_->error());
    return {};
}

Result<void> Memory::copy_from_device(void *destination, const DeviceBuffer &buffer, std::size_t size) {
    if (context_ == nullptr || destination == nullptr || buffer.context_ != context_ || size > buffer.size_)
        return unexpected(ErrorCode::kInvalidArgument, "device-to-host copy requires a runtime buffer");
    if (!context_->copy_device_to_host(destination, buffer.data_, size))
        return unexpected(ErrorCode::kRuntime,
                          context_->error().empty() ? "invalid device-to-host copy" : context_->error());
    return {};
}

void Memory::attach(NpuRaContext *context) noexcept {
    context_ = context;
}

}  // namespace nds::client
