#include "transport.hh"

#include "nds/wire/transport.hh"

#include <arpa/inet.h>

#include <cerrno>
#include <cstring>
#include <sys/socket.h>
#include <unistd.h>
#include <utility>

namespace nds::client {
namespace {

Result<TcpPeerExchange> accept_control(const std::string &address) {
    const auto parsed = parse_tcp_address(address);
    if (!parsed)
        return unexpected(parsed.error());
    const int listener = socket(AF_INET, SOCK_STREAM, 0);
    int enabled = 1;
    sockaddr_in socket_address{};
    socket_address.sin_family = AF_INET;
    socket_address.sin_port = htons(parsed->port);
    if (listener < 0 || setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &enabled, sizeof(enabled)) != 0 ||
        inet_pton(AF_INET, parsed->ipv4.c_str(), &socket_address.sin_addr) != 1 ||
        bind(listener, reinterpret_cast<const sockaddr *>(&socket_address), sizeof(socket_address)) != 0 ||
        listen(listener, 1) != 0) {
        const int error = errno;
        if (listener >= 0)
            (void)close(listener);
        return unexpected(ErrorCode::kTransport, std::strerror(error));
    }
    const int peer = accept(listener, nullptr, nullptr);
    const int error = errno;
    (void)close(listener);
    if (peer < 0)
        return unexpected(ErrorCode::kTransport, std::strerror(error));
    return TcpPeerExchange(peer);
}

}  // namespace

Result<void> Transport::open(Runtime *runtime, const TransportConfig &config, const ExecutionConfig &execution) {
    return open_common(runtime, config, execution, false);
}

Result<void> Transport::open_server(Runtime *runtime, const TransportConfig &config,
                                    const ExecutionConfig &execution) {
    return open_common(runtime, config, execution, true);
}

Result<void> Transport::open_common(Runtime *runtime, const TransportConfig &config,
                                    const ExecutionConfig &execution, bool server) {
    if (runtime == nullptr || !runtime->initialized() || runtime_ != nullptr)
        return unexpected(ErrorCode::kInvalidArgument, "transport requires one open runtime");
    if (config.qp_count == 0U || config.qp_count > nds::wire::kMaxQpInfoBatch)
        return unexpected(ErrorCode::kInvalidArgument, "transport QP count is outside the supported batch limit");
    if (server && config.listen_address.empty())
        return unexpected(ErrorCode::kInvalidArgument, "server transport requires a listen address");
    if (!server && config.server_address.empty())
        return unexpected(ErrorCode::kInvalidArgument, "client transport requires a server address");

    runtime_ = runtime;
    config_ = config;
    execution_ = execution;
    if (const auto opened = endpoint_.open(runtime_, config_.endpoint); !opened)
        return unexpected(opened.error());

    qps_.reserve(config_.qp_count);
    local_qps_.reserve(config_.qp_count);
    for (std::uint32_t index = 0U; index < config_.qp_count; ++index) {
        auto created = endpoint_.create_qp(config_.qp, execution_.mode);
        if (!created)
            return unexpected(created.error());
        qps_.push_back(std::move(*created));
        const auto local = qps_.back().local_qp_info();
        if (!local)
            return unexpected(local.error());
        local_qps_.push_back(*local);
    }
    if (const auto private_memory = initialize_private_memory(); !private_memory)
        return unexpected(private_memory.error());

    if (server) {
        auto accepted = accept_control(config_.listen_address);
        if (!accepted)
            return unexpected(accepted.error());
        bootstrap_ = std::move(*accepted);
    } else {
        const auto address = parse_tcp_address(config_.server_address);
        if (!address)
            return unexpected(address.error());
        auto connected = TcpPeerExchange::connect(address->ipv4, address->port, config_.tcp_timeout_ms);
        if (!connected)
            return unexpected(connected.error());
        bootstrap_ = std::move(*connected);
    }

    std::vector<nds::transport::QpInfo> peer_qps;
    if (config_.qp_count == 1U) {
        const auto peer = server ? bootstrap_.exchange_as_server(local_qps_.front())
                                 : bootstrap_.exchange_as_client(local_qps_.front());
        if (!peer)
            return unexpected(peer.error());
        peer_qps.push_back(*peer);
    } else {
        const auto peers = server ? bootstrap_.exchange_as_server(local_qps_)
                                  : bootstrap_.exchange_as_client(local_qps_);
        if (!peers)
            return unexpected(peers.error());
        peer_qps = *peers;
    }
    if (peer_qps.size() != qps_.size())
        return unexpected(ErrorCode::kTransport, "peer returned a different QP count");
    for (std::size_t index = 0U; index < qps_.size(); ++index) {
        if (const auto connected = qps_[index].connect(peer_qps[index]); !connected)
            return unexpected(connected.error());
    }
    return ready();
}

TcpPeerExchange *Transport::bootstrap() noexcept {
    return &bootstrap_;
}

const nds::transport::QpInfo &Transport::local_qp_info() const noexcept {
    static const nds::transport::QpInfo empty{};
    return local_qps_.empty() ? empty : local_qps_.front();
}

const std::vector<nds::transport::QpInfo> &Transport::local_qp_infos() const noexcept {
    return local_qps_;
}

Runtime *Transport::runtime() noexcept {
    return runtime_;
}

Endpoint *Transport::endpoint() noexcept {
    return &endpoint_;
}

QueuePair *Transport::qp() noexcept {
    return qp(0U);
}

QueuePair *Transport::qp(std::size_t index) noexcept {
    return index < qps_.size() ? &qps_[index] : nullptr;
}

const std::vector<QueuePair> &Transport::qps() const noexcept {
    return qps_;
}

std::size_t Transport::qp_count() const noexcept {
    return qps_.size();
}

const ExecutionConfig &Transport::execution() const noexcept {
    return execution_;
}

Result<void> Transport::ready() {
    if (qps_.empty())
        return unexpected(ErrorCode::kInvalidArgument, "transport has no QPs");
    for (auto &qp : qps_) {
        const auto port = qp.query_port_status();
        if (!port)
            return unexpected(port.error());
        const auto qp_status = qp.query_status();
        if (!qp_status)
            return unexpected(qp_status.error());
        const auto lite = qp.query_support_lite();
        if (!lite)
            return unexpected(lite.error());
        if (*port != NDS_RA_PORT_STATUS_ACTIVE || *qp_status != NDS_RA_QP_STATUS_CONNECTED ||
            *lite == NDS_RA_LITE_NOT_SUPPORTED)
            return unexpected(ErrorCode::kRa, "client transport is not ready");
    }
    return {};
}

Result<void> Transport::initialize_private_memory() {
    if (execution_.mode == NpuExecutionMode::Ra)
        return {};
    send_wr_ids_.reserve(qps_.size());
    receive_wr_ids_.reserve(qps_.size());
    for (auto &qp : qps_) {
        auto send_wr_ids = runtime_->allocate(config_.qp.send_queue_depth * sizeof(std::uint64_t));
        if (!send_wr_ids)
            return unexpected(send_wr_ids.error());
        auto receive_wr_ids = runtime_->allocate(config_.qp.receive_queue_depth * sizeof(std::uint64_t));
        if (!receive_wr_ids)
            return unexpected(receive_wr_ids.error());
        send_wr_ids_.push_back(std::move(*send_wr_ids));
        receive_wr_ids_.push_back(std::move(*receive_wr_ids));
        if (const auto configured = qp.set_device_wr_id_storage(
                reinterpret_cast<std::uint64_t>(send_wr_ids_.back().data()),
                reinterpret_cast<std::uint64_t>(receive_wr_ids_.back().data()));
            !configured)
            return unexpected(configured.error());
    }
    return {};
}

}  // namespace nds::client
