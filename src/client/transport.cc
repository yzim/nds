#include "transport.hh"

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

std::uint64_t RegisteredRegion::address() const noexcept {
    return reinterpret_cast<std::uint64_t>(info_.address);
}
std::uint64_t RegisteredRegion::length() const noexcept {
    return info_.size;
}

Result<void> Connection::open(const ConnectionConfig &config) {
    config_ = config;
    config_.qp.backend = config.backend.mode;
    if (!context_.initialize(config_.context)) {
        return failure(ErrorCode::kRuntime, context_.error());
    }
    if (!qp_.create(&context_.ra_api(), config_.qp) || !qp_.make_endpoint(&local_)) {
        return failure(ErrorCode::kRa, qp_.error());
    }
    std::string error;
    if (!TcpPeerExchange::connect(config.cpu_ipv4, config.tcp_port, config.tcp_timeout_ms, &bootstrap_, &error)) {
        return failure(ErrorCode::kTransport, std::move(error));
    }
    const PeerExchangeResult exchanged = bootstrap_.exchange_as_client(local_);
    if (!exchanged.ok) {
        return failure(ErrorCode::kTransport,
                       exchanged.error.empty() ? "server endpoint exchange failed" : std::move(exchanged.error));
    }
    if (!qp_.connect(exchanged.peer)) {
        return failure(ErrorCode::kRa, qp_.error());
    }
    return ready();
}

Result<void> Connection::allocate(std::size_t size, DeviceBuffer *buffer) {
    if (buffer == nullptr || buffer->data_ != nullptr || size == 0U ||
        !context_.allocate_device_memory(size, &buffer->data_)) {
        return failure(ErrorCode::kRuntime, context_.error().empty() ? "invalid device allocation" : context_.error());
    }
    buffer->context_ = &context_;
    buffer->size_ = size;
    return {};
}

Result<void> Connection::register_memory(DeviceBuffer *buffer, RegisteredRegion *region) {
    if (buffer == nullptr || region == nullptr || region->handle_ != nullptr ||
        !qp_.register_memory(buffer->data_, buffer->size_, NDS_RA_ACCESS_DIRECT_NPU, &region->info_,
                             &region->handle_)) {
        return failure(ErrorCode::kRa, qp_.error().empty() ? "invalid memory registration" : qp_.error());
    }
    region->qp_ = &qp_;
    return {};
}

Result<void> Connection::copy_to_device(DeviceBuffer *buffer, const void *source, std::size_t size) {
    if (buffer == nullptr || source == nullptr || size > buffer->size_ ||
        !context_.copy_host_to_device(buffer->data_, source, size)) {
        return failure(ErrorCode::kRuntime,
                       context_.error().empty() ? "invalid host-to-device copy" : context_.error());
    }
    return {};
}

Result<void> Connection::copy_from_device(void *destination, const DeviceBuffer &buffer, std::size_t size) {
    if (destination == nullptr || size > buffer.size_ ||
        !context_.copy_device_to_host(destination, buffer.data_, size)) {
        return failure(ErrorCode::kRuntime,
                       context_.error().empty() ? "invalid device-to-host copy" : context_.error());
    }
    return {};
}

Result<void> Connection::send(const RegisteredRegion &source, std::uint32_t length) {
    std::string error;
    if (!post_send(&context_, &qp_, config_.backend, source.address(), length, source.info_.local_key, &error)) {
        return failure(ErrorCode::kRa, std::move(error));
    }
    return {};
}

Result<RemoteRegion> Connection::remote_region(const RegisteredRegion &region) const {
    if (region.handle_ == nullptr) {
        return failure(ErrorCode::kRa, "registered region is not available");
    }
    return RemoteRegion{region.address(), region.length(), region.info_.remote_key};
}

TcpPeerExchange *Connection::bootstrap() noexcept {
    return &bootstrap_;
}

const nds_transport_endpoint &Connection::local_endpoint() const noexcept {
    return local_;
}

Result<void> Connection::ready() {
    int port = -1;
    int qp_status = -1;
    int lite = -1;
    if (!qp_.query_port_status(&port) || !qp_.query_status(&qp_status) || !qp_.query_support_lite(&lite) ||
        port != NDS_RA_PORT_STATUS_ACTIVE || qp_status != NDS_RA_QP_STATUS_CONNECTED ||
        lite == NDS_RA_LITE_NOT_SUPPORTED) {
        return failure(ErrorCode::kRa, qp_.error().empty() ? "client connection is not ready" : qp_.error());
    }
    return {};
}

}  // namespace nds::client
