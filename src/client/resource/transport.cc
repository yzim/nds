#include "transport.hh"

#include <utility>

namespace nds::client {

Result<void> Transport::open(Runtime *runtime, const TransportConfig &config, const BackendConfig &backend) {
    if (runtime == nullptr || !runtime->initialized() || runtime_ != nullptr)
        return unexpected(ErrorCode::kInvalidArgument, "transport requires one open runtime");
    runtime_ = runtime;
    config_ = config;
    backend_ = backend;
    if (const auto opened = endpoint_.open(runtime_, config_.endpoint); !opened)
        return unexpected(opened.error());
    auto created = endpoint_.create_qp(config_.qp, backend_.mode);
    if (!created)
        return unexpected(created.error());
    qp_ = std::move(*created);
    const auto local = qp_.local_qp_info();
    if (!local)
        return unexpected(local.error());
    local_ = *local;
    if (const auto private_memory = initialize_private_memory(); !private_memory)
        return unexpected(private_memory.error());
    const auto server = parse_tcp_address(config_.server_address);
    if (!server)
        return unexpected(server.error());
    auto connected = TcpPeerExchange::connect(server->ipv4, server->port, config_.tcp_timeout_ms);
    if (!connected) {
        return unexpected(connected.error());
    }
    bootstrap_ = std::move(*connected);
    const auto peer = bootstrap_.exchange_as_client(local_);
    if (!peer)
        return unexpected(peer.error());
    if (const auto connected = qp_.connect(*peer); !connected)
        return unexpected(connected.error());
    return ready();
}

TcpPeerExchange *Transport::bootstrap() noexcept {
    return &bootstrap_;
}

const nds::transport::QpInfo &Transport::local_qp_info() const noexcept {
    return local_;
}

Runtime *Transport::runtime() noexcept {
    return runtime_;
}

Endpoint *Transport::endpoint() noexcept {
    return &endpoint_;
}

QueuePair *Transport::qp() noexcept {
    return &qp_;
}

const BackendConfig &Transport::backend() const noexcept {
    return backend_;
}

Result<void> Transport::ready() {
    const auto port = qp_.query_port_status();
    if (!port)
        return unexpected(port.error());
    const auto qp_status = qp_.query_status();
    if (!qp_status)
        return unexpected(qp_status.error());
    const auto lite = qp_.query_support_lite();
    if (!lite)
        return unexpected(lite.error());
    if (*port != NDS_RA_PORT_STATUS_ACTIVE || *qp_status != NDS_RA_QP_STATUS_CONNECTED ||
        *lite == NDS_RA_LITE_NOT_SUPPORTED) {
        return unexpected(ErrorCode::kRa, "client transport is not ready");
    }
    return {};
}

Result<void> Transport::initialize_private_memory() {
    if (backend_.mode == NpuBackend::Ra)
        return {};
    auto send_wr_ids = runtime_->allocate(config_.qp.send_queue_depth * sizeof(std::uint64_t));
    if (!send_wr_ids) {
        return unexpected(send_wr_ids.error());
    }
    send_wr_ids_ = std::move(*send_wr_ids);
    auto receive_wr_ids = runtime_->allocate(config_.qp.receive_queue_depth * sizeof(std::uint64_t));
    if (!receive_wr_ids) {
        return unexpected(receive_wr_ids.error());
    }
    receive_wr_ids_ = std::move(*receive_wr_ids);
    return qp_.set_device_wr_id_storage(reinterpret_cast<std::uint64_t>(send_wr_ids_.data()),
                                        reinterpret_cast<std::uint64_t>(receive_wr_ids_.data()));
}

}  // namespace nds::client
