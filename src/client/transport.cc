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

bool Connection::open(const ConnectionConfig &config, std::string *error) {
    if (error == nullptr)
        return false;
    config_ = config;
    config_.qp.backend = config.backend.mode;
    if (!context_.initialize(config_.context)) {
        *error = context_.error();
        return false;
    }
    if (!qp_.create(&context_.ra_api(), config_.qp) || !qp_.make_endpoint(&local_)) {
        *error = qp_.error();
        return false;
    }
    if (!TcpPeerExchange::connect(config.cpu_ipv4, config.tcp_port, config.tcp_timeout_ms, &bootstrap_, error)) {
        return false;
    }
    const PeerExchangeResult exchanged = bootstrap_.exchange_as_client(local_);
    if (!exchanged.ok) {
        *error = exchanged.error.empty() ? "CPU endpoint exchange failed" : exchanged.error;
        return false;
    }
    if (!qp_.connect(exchanged.peer)) {
        *error = qp_.error();
        return false;
    }
    return ready(error);
}

bool Connection::allocate(std::size_t size, DeviceBuffer *buffer, std::string *error) {
    if (buffer == nullptr || error == nullptr || buffer->data_ != nullptr || size == 0U ||
        !context_.allocate_device_memory(size, &buffer->data_)) {
        if (error != nullptr)
            *error = context_.error().empty() ? "invalid device allocation" : context_.error();
        return false;
    }
    buffer->context_ = &context_;
    buffer->size_ = size;
    return true;
}

bool Connection::register_memory(DeviceBuffer *buffer, RegisteredRegion *region, std::string *error) {
    if (buffer == nullptr || region == nullptr || error == nullptr || region->handle_ != nullptr ||
        !qp_.register_memory(buffer->data_, buffer->size_, NDS_RA_ACCESS_DIRECT_NPU, &region->info_,
                             &region->handle_)) {
        if (error != nullptr)
            *error = qp_.error().empty() ? "invalid memory registration" : qp_.error();
        return false;
    }
    region->qp_ = &qp_;
    return true;
}

bool Connection::copy_to_device(DeviceBuffer *buffer, const void *source, std::size_t size, std::string *error) {
    if (buffer == nullptr || source == nullptr || error == nullptr || size > buffer->size_ ||
        !context_.copy_host_to_device(buffer->data_, source, size)) {
        if (error != nullptr)
            *error = context_.error().empty() ? "invalid host-to-device copy" : context_.error();
        return false;
    }
    return true;
}

bool Connection::copy_from_device(void *destination, const DeviceBuffer &buffer, std::size_t size, std::string *error) {
    if (destination == nullptr || error == nullptr || size > buffer.size_ ||
        !context_.copy_device_to_host(destination, buffer.data_, size)) {
        if (error != nullptr)
            *error = context_.error().empty() ? "invalid device-to-host copy" : context_.error();
        return false;
    }
    return true;
}

bool Connection::send(const RegisteredRegion &source, std::uint32_t length, std::string *error) {
    return post_send(&context_, &qp_, config_.backend, source.address(), length, source.info_.local_key, error);
}

bool Connection::remote_region(const RegisteredRegion &region, RemoteRegion *remote, std::string *error) const {
    if (remote == nullptr || error == nullptr || region.handle_ == nullptr) {
        if (error != nullptr)
            *error = "registered region is not available";
        return false;
    }
    *remote = {region.address(), region.length(), region.info_.remote_key};
    return true;
}

TcpPeerExchange *Connection::bootstrap() noexcept {
    return &bootstrap_;
}

const nds_transport_endpoint &Connection::local_endpoint() const noexcept {
    return local_;
}

bool Connection::ready(std::string *error) {
    int port = -1;
    int qp_status = -1;
    int lite = -1;
    if (error == nullptr || !qp_.query_port_status(&port) || !qp_.query_status(&qp_status) ||
        !qp_.query_support_lite(&lite) || port != NDS_RA_PORT_STATUS_ACTIVE ||
        qp_status != NDS_RA_QP_STATUS_CONNECTED || lite == NDS_RA_LITE_NOT_SUPPORTED) {
        if (error != nullptr)
            *error = qp_.error().empty() ? "NPU transport connection is not ready" : qp_.error();
        return false;
    }
    return true;
}

}  // namespace nds::client
