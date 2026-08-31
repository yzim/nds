#include "transport.hh"

#include "transport_protocol.hh"

#include <algorithm>
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
    if (const auto received = channel->receive(std::as_writable_bytes(std::span{&encoded, 1U})); !received)
        return Error{received.error()};
    nds::transport::TransportInfo info{};
    if (nds::transport::decode(&encoded, &info) != nds::transport::CodecResult::Ok)
        return Error{ErrorCode::kTransport, "invalid transport information"};
    return info;
}

}  // namespace

Result<void> TransportListener::open(const TransportConfig &config) {
    if (tcp_listener_.is_open() || config.max_qp_count == 0U || config.max_qp_count > nds::wire::kMaxQpInfoBatch)
        return Error{ErrorCode::kInvalidArgument, "invalid server QP-count limit"};
    const auto listen_address = parse_tcp_address(config.listen_address);
    if (!listen_address)
        return Error{listen_address.error()};
    auto listener = TcpListener::listen(listen_address->ipv4, listen_address->port, kTcpListenBacklog);
    if (!listener)
        return Error{listener.error()};
    tcp_listener_ = std::move(*listener);
    config_ = config;
    return {};
}

Result<void> TransportListener::accept(Transport *transport) {
    if (!tcp_listener_.is_open() || transport == nullptr)
        return Error{ErrorCode::kInvalidArgument, "open listener and transport are required"};
    auto exchange_channel = tcp_listener_.accept();
    if (!exchange_channel)
        return Error{exchange_channel.error()};
    const auto count_request = receive_transport_info(&*exchange_channel);
    if (!count_request)
        return Error{count_request.error()};
    if (count_request->kind != nds::transport::TransportInfoKind::QpCountRequest)
        return Error{ErrorCode::kTransport, "expected a QP-count request"};
    const std::uint32_t qp_count = std::min(count_request->qp_count, config_.max_qp_count);
    const nds::transport::TransportInfo count_response{
        nds::transport::TransportInfoKind::QpCountResponse, qp_count, {}};
    NDS_RETURN_IF_ERROR(send_transport_info(&*exchange_channel, count_response));
    return transport->open(std::move(*exchange_channel), config_, qp_count);
}

Result<void> Transport::open(TcpConnection exchange_channel, const TransportConfig &config, std::uint32_t qp_count) {
    if (qp_count == 0U || qp_count > config.max_qp_count || qp_count > nds::wire::kMaxQpInfoBatch)
        return Error{ErrorCode::kInvalidArgument, "invalid negotiated QP count"};
    if (const auto opened = backend_.open(config.backend, qp_count); !opened)
        return Error{opened.error()};
    const std::vector<nds::QpInfo> &local = backend_.local_qp_infos();
    exchange_channel_ = std::move(exchange_channel);
    const auto peer_info = receive_transport_info(&exchange_channel_);
    if (!peer_info)
        return Error{peer_info.error()};
    if (peer_info->kind != nds::transport::TransportInfoKind::QpEndpoints || peer_info->qp_count != qp_count)
        return Error{ErrorCode::kTransport, "peer returned mismatched QP information"};
    nds::transport::TransportInfo local_info{nds::transport::TransportInfoKind::QpEndpoints, qp_count, {}};
    std::copy(local.begin(), local.end(), local_info.qps.begin());
    NDS_RETURN_IF_ERROR(send_transport_info(&exchange_channel_, local_info));
    std::vector<nds::QpInfo> peers(peer_info->qps.begin(), peer_info->qps.begin() + qp_count);
    if (const auto connected = backend_.connect(peers); !connected)
        return Error{connected.error()};
    return {};
}

Result<RegisteredRegion> Transport::prepare_receive(void *buffer, std::size_t length) {
    return prepare_receive(0U, buffer, length);
}

Result<RegisteredRegion> Transport::prepare_receive(std::size_t qp_index, void *buffer, std::size_t length) {
    auto registered = backend_.register_memory(buffer, length, IBV_ACCESS_LOCAL_WRITE);
    if (!registered)
        return Error{registered.error()};
    if (const auto posted = backend_.post_receive(qp_index, *registered); !posted)
        return Error{posted.error()};
    return std::move(*registered);
}

Result<void> Transport::receive(std::uint32_t timeout_ms) {
    return receive(0U, timeout_ms);
}
Result<void> Transport::receive(std::size_t qp_index, std::uint32_t timeout_ms) {
    if (const auto received = backend_.wait_receive(qp_index, timeout_ms); !received)
        return Error{received.error()};
    return {};
}
Result<void> Transport::send(const RegisteredRegion &local, std::uint32_t length) {
    return send(0U, local, length);
}
Result<void> Transport::send(std::size_t qp_index, const RegisteredRegion &local, std::uint32_t length) {
    if (const auto sent = backend_.send(qp_index, local, length); !sent)
        return Error{sent.error()};
    return {};
}
Result<RegisteredRegion> Transport::register_memory(void *buffer, std::size_t length, MemoryAccess access) {
    int backend_access = 0;
    if (access == MemoryAccess::LocalWrite)
        backend_access = IBV_ACCESS_LOCAL_WRITE;
    else if (access == MemoryAccess::RemoteRead)
        backend_access = IBV_ACCESS_REMOTE_READ;
    else if (access == MemoryAccess::RemoteWrite)
        backend_access = IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE;
    auto registered = backend_.register_memory(buffer, length, backend_access);
    if (!registered)
        return Error{registered.error()};
    return std::move(*registered);
}
Result<void> Transport::read(const RegisteredRegion &local, std::uint64_t address, std::uint32_t key,
                             std::uint32_t length) {
    return read(0U, local, address, key, length);
}
Result<void> Transport::read(std::size_t qp_index, const RegisteredRegion &local, std::uint64_t address,
                             std::uint32_t key, std::uint32_t length) {
    if (const auto read = backend_.read(qp_index, local, address, key, length); !read)
        return Error{read.error()};
    return {};
}
Result<void> Transport::write(const RegisteredRegion &local, std::uint64_t address, std::uint32_t key,
                              std::uint32_t length) {
    return write(0U, local, address, key, length);
}
Result<void> Transport::write(std::size_t qp_index, const RegisteredRegion &local, std::uint64_t address,
                              std::uint32_t key, std::uint32_t length) {
    if (const auto written = backend_.write(qp_index, local, address, key, length); !written)
        return Error{written.error()};
    return {};
}
std::size_t Transport::qp_count() const noexcept {
    return backend_.qp_count();
}
TcpConnection *Transport::exchange_channel() noexcept {
    return &exchange_channel_;
}

}  // namespace nds::server
