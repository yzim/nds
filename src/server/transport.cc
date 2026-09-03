#include "transport.hh"

#include "transport_protocol.hh"

#include <algorithm>
#include <limits>
#include <span>
#include <utility>

namespace nds::server {

namespace {

constexpr int kTcpListenBacklog = 8;

Result<void> send_transport_info(TcpConnection *channel, const nds::transport::TransportInfo &info) {
    if (channel == nullptr)
        return Error{ErrorCode::kInvalidArgument, "TCP connection is required"};
    nds::wire::TransportInfo encoded{};
    if (nds::transport::encode(&info, &encoded) != nds::transport::CodecResult::Ok)
        return Error{ErrorCode::kTransport, "cannot encode transport information"};
    return channel->send(std::as_bytes(std::span{&encoded, 1U}));
}

Result<nds::transport::TransportInfo> receive_transport_info(TcpConnection *channel) {
    if (channel == nullptr)
        return Error{ErrorCode::kInvalidArgument, "TCP connection is required"};
    nds::wire::TransportInfo encoded{};
    if (const auto received = channel->receive(std::as_writable_bytes(std::span{&encoded, 1U})); !received.ok())
        return received.error();
    nds::transport::TransportInfo info{};
    if (nds::transport::decode(&encoded, &info) != nds::transport::CodecResult::Ok)
        return Error{ErrorCode::kTransport, "invalid transport information"};
    return info;
}

}  // namespace

Result<void> TransportListener::open(const TransportConfig &config) {
    if (tcp_listener_.is_open() || config.max_qp_count == 0U || config.max_qp_count > nds::wire::kMaxQpInfoBatch)
        return Error{ErrorCode::kInvalidArgument, "invalid server QP-count limit"};
    if (config.completion_timeout_ms == 0U ||
        config.completion_timeout_ms > static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max()))
        return Error{ErrorCode::kInvalidArgument, "invalid server completion-timeout limit"};
    const auto listen_address = parse_tcp_address(config.listen_address);
    if (!listen_address.ok())
        return listen_address.error();
    auto listener = TcpListener::listen(listen_address.value().ipv4, listen_address.value().port, kTcpListenBacklog);
    if (!listener.ok())
        return listener.error();
    tcp_listener_ = std::move(listener).value();
    config_ = config;
    return {};
}

Result<void> TransportListener::accept(Transport *transport) {
    if (!tcp_listener_.is_open() || transport == nullptr || transport->opened())
        return Error{ErrorCode::kInvalidArgument, "open listener and transport are required"};
    auto exchange_channel = tcp_listener_.accept();
    if (!exchange_channel.ok())
        return exchange_channel.error();
    const auto count_request = receive_transport_info(&exchange_channel.value());
    if (!count_request.ok())
        return count_request.error();
    if (count_request.value().kind != nds::transport::TransportInfoKind::QpCountRequest)
        return Error{ErrorCode::kTransport, "expected a QP-count request"};
    const std::uint32_t qp_count = std::min(count_request.value().qp_count, config_.max_qp_count);
    const nds::transport::TransportInfo count_response{
        nds::transport::TransportInfoKind::QpCountResponse, qp_count, {}};
    NDS_RETURN_IF_ERROR(send_transport_info(&exchange_channel.value(), count_response));
    return transport->open(std::move(exchange_channel).value(), config_, qp_count);
}

Result<void> Transport::open(TcpConnection exchange_channel, const TransportConfig &config, std::uint32_t qp_count) {
    if (!exchange_channel.is_open())
        return Error{ErrorCode::kInvalidArgument, "an open CPU transport exchange channel is required"};
    if (opened())
        return Error{ErrorCode::kInvalidArgument, "CPU transport is already open"};
    if (qp_count == 0U || qp_count > config.max_qp_count || qp_count > nds::wire::kMaxQpInfoBatch)
        return Error{ErrorCode::kInvalidArgument, "invalid negotiated QP count"};
    if (config.completion_timeout_ms == 0U ||
        config.completion_timeout_ms > static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max()))
        return Error{ErrorCode::kInvalidArgument, "invalid CPU transport completion timeout"};
    if (const auto opened = endpoint_.open(config.endpoint); !opened.ok())
        return opened.error();
    completion_timeout_ms_ = config.completion_timeout_ms;

    qps_.reserve(qp_count);
    local_qps_.reserve(qp_count);
    for (std::uint32_t index = 0U; index < qp_count; ++index) {
        auto qp = endpoint_.create_qp();
        if (!qp.ok())
            return qp.error();
        local_qps_.push_back(qp.value().local_qp_info());
        qps_.push_back(std::move(qp).value());
    }

    exchange_channel_ = std::move(exchange_channel);
    const auto peer_info = receive_transport_info(&exchange_channel_);
    if (!peer_info.ok())
        return peer_info.error();
    if (peer_info.value().kind != nds::transport::TransportInfoKind::QpEndpoints ||
        peer_info.value().qp_count != qp_count)
        return Error{ErrorCode::kTransport, "peer returned mismatched QP information"};
    nds::transport::TransportInfo local_info{nds::transport::TransportInfoKind::QpEndpoints, qp_count, {}};
    std::copy(local_qps_.begin(), local_qps_.end(), local_info.qps.begin());
    NDS_RETURN_IF_ERROR(send_transport_info(&exchange_channel_, local_info));
    for (std::size_t index = 0U; index < qps_.size(); ++index) {
        if (const auto connected = qps_[index].connect(peer_info.value().qps[index]); !connected.ok())
            return connected.error();
    }
    return {};
}

QueuePair *Transport::queue_pair(std::size_t qp_index) noexcept {
    return qp_index < qps_.size() ? &qps_[qp_index] : nullptr;
}

Result<void> Transport::post_receive(const MemoryRegion &region) {
    return post_receive(0U, region);
}

Result<void> Transport::post_receive(std::size_t qp_index, const MemoryRegion &region) {
    QueuePair *qp = queue_pair(qp_index);
    if (qp == nullptr)
        return Error{ErrorCode::kInvalidArgument, "CPU transport QP index is out of range"};
    return qp->post_receive(region);
}

Result<void> Transport::wait_receive(std::uint32_t timeout_ms) {
    return wait_receive(0U, timeout_ms);
}

Result<void> Transport::wait_receive(std::size_t qp_index, std::uint32_t timeout_ms) {
    QueuePair *qp = queue_pair(qp_index);
    if (qp == nullptr)
        return Error{ErrorCode::kInvalidArgument, "CPU transport QP index is out of range"};
    return qp->wait_receive(timeout_ms);
}

Result<void> Transport::send(const MemoryRegion &local, std::uint32_t length) {
    return send(0U, local, length);
}

Result<void> Transport::send(std::size_t qp_index, const MemoryRegion &local, std::uint32_t length) {
    QueuePair *qp = queue_pair(qp_index);
    if (qp == nullptr)
        return Error{ErrorCode::kInvalidArgument, "CPU transport QP index is out of range"};
    return qp->send(local, length, completion_timeout_ms_);
}

Result<MemoryRegion> Transport::register_memory(void *buffer, std::size_t length, MemoryAccess access) {
    int verbs_access = 0;
    if (access == MemoryAccess::LocalWrite)
        verbs_access = IBV_ACCESS_LOCAL_WRITE;
    else if (access == MemoryAccess::RemoteRead)
        verbs_access = IBV_ACCESS_REMOTE_READ;
    else if (access == MemoryAccess::RemoteWrite)
        verbs_access = IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE;
    else if (access != MemoryAccess::LocalRead)
        return Error{ErrorCode::kInvalidArgument, "unsupported CPU memory access mode"};
    return endpoint_.reg_mr(buffer, length, verbs_access);
}

Result<void> Transport::read(const MemoryRegion &local, std::uint64_t address, std::uint32_t key,
                             std::uint32_t length) {
    return read(0U, local, address, key, length);
}

Result<void> Transport::read(std::size_t qp_index, const MemoryRegion &local, std::uint64_t address, std::uint32_t key,
                             std::uint32_t length) {
    QueuePair *qp = queue_pair(qp_index);
    if (qp == nullptr)
        return Error{ErrorCode::kInvalidArgument, "CPU transport QP index is out of range"};
    return qp->read(local, address, key, length, completion_timeout_ms_);
}

Result<void> Transport::write(const MemoryRegion &local, std::uint64_t address, std::uint32_t key,
                              std::uint32_t length) {
    return write(0U, local, address, key, length);
}

Result<void> Transport::write(std::size_t qp_index, const MemoryRegion &local, std::uint64_t address, std::uint32_t key,
                              std::uint32_t length) {
    QueuePair *qp = queue_pair(qp_index);
    if (qp == nullptr)
        return Error{ErrorCode::kInvalidArgument, "CPU transport QP index is out of range"};
    return qp->write(local, address, key, length, completion_timeout_ms_);
}

Result<void> Transport::read_batch(std::size_t qp_index, std::span<const TransferRequest> requests) {
    QueuePair *qp = queue_pair(qp_index);
    if (qp == nullptr || requests.empty())
        return Error{ErrorCode::kInvalidArgument, "CPU transport RDMA-read batch is invalid"};
    NDS_ASSIGN_OR_RETURN(const std::vector<std::uint64_t> wr_ids, qp->post_transfer_batch(IBV_WR_RDMA_READ, requests));
    // RC completion ordering makes the final signaled WQE a completion fence
    // for the preceding payload WQEs in this linked batch.
    return qp->poll(IBV_WC_RDMA_READ, wr_ids.back(), completion_timeout_ms_);
}

Result<void> Transport::write_batch(std::size_t qp_index, std::span<const TransferRequest> requests) {
    QueuePair *qp = queue_pair(qp_index);
    if (qp == nullptr || requests.empty())
        return Error{ErrorCode::kInvalidArgument, "CPU transport RDMA-write batch is invalid"};
    NDS_ASSIGN_OR_RETURN(const std::vector<std::uint64_t> wr_ids, qp->post_transfer_batch(IBV_WR_RDMA_WRITE, requests));
    return qp->poll(IBV_WC_RDMA_WRITE, wr_ids.back(), completion_timeout_ms_);
}

std::size_t Transport::qp_count() const noexcept {
    return qps_.size();
}

bool Transport::opened() const noexcept {
    return exchange_channel_.is_open() || endpoint_.opened();
}

TcpConnection *Transport::exchange_channel() noexcept {
    return &exchange_channel_;
}

}  // namespace nds::server
