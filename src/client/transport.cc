#include "transport.hh"

#include <chrono>
#include <thread>

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

Result<void> Transport::open(const TransportConfig &config) {
    config_ = config;
    if (!context_.initialize(config_.context)) {
        return unexpected(ErrorCode::kRuntime, context_.error());
    }
    if (!qp_.create(&context_.ra_api(), config_.qp, config.execution) || !qp_.make_endpoint(&local_)) {
        return unexpected(ErrorCode::kRa, qp_.error());
    }
    if (config.execution != NpuExecutionMode::HostRa) {
        if (const auto allocated = allocate(config_.qp.send_queue_depth * sizeof(std::uint64_t), &send_wr_ids_);
            !allocated)
            return unexpected(allocated.error());
        if (const auto allocated =
                allocate(config_.qp.receive_queue_depth * sizeof(std::uint64_t), &receive_wr_ids_);
            !allocated)
            return unexpected(allocated.error());
        if (const auto storage = qp_.set_device_wr_id_storage(
                reinterpret_cast<std::uint64_t>(send_wr_ids_.data()),
                reinterpret_cast<std::uint64_t>(receive_wr_ids_.data()));
            !storage)
            return unexpected(storage.error());
    }
    if (const auto connected =
            TcpPeerExchange::connect(config.cpu_ipv4, config.tcp_port, config.tcp_timeout_ms, &bootstrap_);
        !connected) {
        return unexpected(connected.error());
    }
    const auto peer = bootstrap_.exchange_as_client(local_);
    if (!peer) {
        return unexpected(peer.error());
    }
    if (!qp_.connect(*peer)) {
        return unexpected(ErrorCode::kRa, qp_.error());
    }
    return ready();
}

Result<void> Transport::allocate(std::size_t size, DeviceBuffer *buffer) {
    if (buffer == nullptr || buffer->context_ != nullptr || buffer->data_ != nullptr || size == 0U)
        return unexpected(ErrorCode::kInvalidArgument, "transport allocation requires an empty device buffer");
    if (!context_.allocate_device_memory(size, &buffer->data_)) {
        return unexpected(ErrorCode::kRuntime,
                          context_.error().empty() ? "invalid device allocation" : context_.error());
    }
    buffer->context_ = &context_;
    buffer->size_ = size;
    return {};
}

Result<void> Transport::register_memory(DeviceBuffer *buffer, RegisteredRegion *region) {
    if (buffer == nullptr || buffer->context_ != &context_ || buffer->data_ == nullptr || region == nullptr ||
        region->qp_ != nullptr || region->handle_ != nullptr) {
        return unexpected(ErrorCode::kInvalidArgument,
                          "memory registration requires a device buffer owned by this transport and an empty region");
    }
    if (!qp_.register_memory(buffer->data_, buffer->size_, NDS_RA_ACCESS_DIRECT_NPU, &region->info_,
                             &region->handle_)) {
        return unexpected(ErrorCode::kRa, qp_.error().empty() ? "invalid memory registration" : qp_.error());
    }
    region->qp_ = &qp_;
    return {};
}

Result<void> Transport::copy_to_device(DeviceBuffer *buffer, const void *source, std::size_t size) {
    if (buffer == nullptr || buffer->context_ != &context_ || source == nullptr || size > buffer->size_)
        return unexpected(ErrorCode::kInvalidArgument, "host-to-device copy requires a buffer from this transport");
    if (!context_.copy_host_to_device(buffer->data_, source, size)) {
        return unexpected(ErrorCode::kRuntime,
                          context_.error().empty() ? "invalid host-to-device copy" : context_.error());
    }
    return {};
}

Result<void> Transport::copy_from_device(void *destination, const DeviceBuffer &buffer, std::size_t size) {
    if (destination == nullptr || buffer.context_ != &context_ || size > buffer.size_)
        return unexpected(ErrorCode::kInvalidArgument, "device-to-host copy requires a buffer from this transport");
    if (!context_.copy_device_to_host(destination, buffer.data_, size)) {
        return unexpected(ErrorCode::kRuntime,
                          context_.error().empty() ? "invalid device-to-host copy" : context_.error());
    }
    return {};
}

Result<void> Transport::send(const RegisteredRegion &source, std::uint32_t length) {
    if (const auto posted = post(WorkRequestOpcode::Send, source, 0U, 0U, length); !posted)
        return unexpected(posted.error());
    return wait_for_send_completion(5000U);
}

Result<void> Transport::post_receive(const RegisteredRegion &destination, std::uint64_t wr_id) {
    if (destination.qp_ != &qp_ || destination.handle_ == nullptr)
        return unexpected(ErrorCode::kInvalidArgument,
                          "receive post requires a registered region from this transport");
    return post_recv_wr(&context_, &qp_, config_.execution, config_.rma,
                        {{destination.address(), static_cast<std::uint32_t>(destination.length()),
                          destination.info_.local_key}, wr_id});
}

Result<std::uint32_t> Transport::poll(CompletionQueue queue, nds_ra_completion *completions,
                                      std::uint32_t max_entries) {
    return poll_cq(&context_, &qp_, config_.execution, config_.rma, queue, completions, max_entries);
}

Result<void> Transport::read(const RegisteredRegion &local, std::uint64_t remote_address,
                             std::uint32_t remote_key, std::uint32_t length) {
    return post(WorkRequestOpcode::RdmaRead, local, remote_address, remote_key, length);
}

Result<void> Transport::write(const RegisteredRegion &local, std::uint64_t remote_address,
                              std::uint32_t remote_key, std::uint32_t length) {
    return post(WorkRequestOpcode::RdmaWrite, local, remote_address, remote_key, length);
}

Result<void> Transport::post(WorkRequestOpcode opcode, const RegisteredRegion &local,
                             std::uint64_t remote_address, std::uint32_t remote_key, std::uint32_t length) {
    if (local.qp_ != &qp_ || local.handle_ == nullptr || length == 0U || length > local.info_.size) {
        return unexpected(ErrorCode::kInvalidArgument,
                          "transport post requires a local region from this transport and a valid length");
    }
    return post_send_wr(&context_, &qp_, config_.execution, config_.rma,
                        {opcode, {local.address(), length, local.info_.local_key}, remote_address, remote_key});
}

Result<void> Transport::wait_for_send_completion(std::uint32_t timeout_ms) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (std::chrono::steady_clock::now() < deadline) {
        nds_ra_completion completion{};
        const auto count = poll(CompletionQueue::Send, &completion, 1U);
        if (!count)
            return unexpected(count.error());
        if (*count == 1U) {
            if (completion.status != 0)
                return unexpected(ErrorCode::kRa, "send completion reports an RNIC error");
            return {};
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return unexpected(ErrorCode::kRa, "timed out waiting for send CQ completion");
}

Result<RemoteRegion> Transport::remote_region(const RegisteredRegion &region) const {
    if (region.qp_ != &qp_ || region.handle_ == nullptr)
        return unexpected(ErrorCode::kInvalidArgument, "registered region does not belong to this transport");
    return RemoteRegion{region.address(), region.length(), region.info_.remote_key};
}

TcpPeerExchange *Transport::bootstrap() noexcept {
    return &bootstrap_;
}

const nds_transport_endpoint &Transport::local_endpoint() const noexcept {
    return local_;
}

Result<void> Transport::ready() {
    int port = -1;
    int qp_status = -1;
    int lite = -1;
    if (!qp_.query_port_status(&port) || !qp_.query_status(&qp_status) || !qp_.query_support_lite(&lite) ||
        port != NDS_RA_PORT_STATUS_ACTIVE || qp_status != NDS_RA_QP_STATUS_CONNECTED ||
        lite == NDS_RA_LITE_NOT_SUPPORTED) {
        return unexpected(ErrorCode::kRa, qp_.error().empty() ? "client transport is not ready" : qp_.error());
    }
    return {};
}

}  // namespace nds::client
