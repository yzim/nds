#include "transport.hh"

#include <utility>

namespace nds::client {

Result<void> Transport::open(Runtime *runtime, const TransportConfig &config, const BackendConfig &backend) {
    if (runtime == nullptr || !runtime->initialized() || runtime_ != nullptr)
        return unexpected(ErrorCode::kInvalidArgument, "transport requires one open runtime");
    if (config.qp_count == 0U || config.qp_count > nds::wire::kMaxQpInfoBatch)
        return unexpected(ErrorCode::kInvalidArgument, "transport QP count is outside the supported batch limit");
    runtime_ = runtime;
    config_ = config;
    backend_ = backend;
    if (const auto opened = endpoint_.open(runtime_, config_.endpoint); !opened)
        return unexpected(opened.error());
    qps_.reserve(config_.qp_count);
    local_qps_.reserve(config_.qp_count);
    for (std::uint32_t index = 0U; index < config_.qp_count; ++index) {
        auto created = endpoint_.create_qp(config_.qp, backend_.mode);
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
    const auto server = parse_tcp_address(config_.server_address);
    if (!server)
        return unexpected(server.error());
    auto connected = TcpPeerExchange::connect(server->ipv4, server->port, config_.tcp_timeout_ms);
    if (!connected) {
        return unexpected(connected.error());
    }
    bootstrap_ = std::move(*connected);
    if (qps_.size() == 1U) {
        const auto peer = bootstrap_.exchange_as_client(local_qps_.front());
        if (!peer)
            return unexpected(peer.error());
        if (const auto qp_connected = qps_.front().connect(*peer); !qp_connected)
            return unexpected(qp_connected.error());
    } else {
        const auto peers = bootstrap_.exchange_as_client(local_qps_);
        if (!peers)
            return unexpected(peers.error());
        if (peers->size() != qps_.size())
            return unexpected(ErrorCode::kTransport, "peer returned a different QP count");
        for (std::size_t index = 0U; index < qps_.size(); ++index) {
            if (const auto qp_connected = qps_[index].connect((*peers)[index]); !qp_connected)
                return unexpected(qp_connected.error());
        }
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

std::size_t Transport::qp_count() const noexcept {
    return qps_.size();
}

const BackendConfig &Transport::backend() const noexcept {
    return backend_;
}

Result<void> Transport::ready() {
    if (qps_.empty())
        return unexpected(ErrorCode::kInvalidArgument, "transport has no QPs");
    for (QueuePair &qp : qps_) {
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
            *lite == NDS_RA_LITE_NOT_SUPPORTED) {
            return unexpected(ErrorCode::kRa, "client transport is not ready");
        }
    }
    return {};
}

Result<void> Transport::initialize_private_memory() {
    if (backend_.mode == NpuBackend::Ra)
        return {};
    send_wr_ids_.reserve(qps_.size());
    receive_wr_ids_.reserve(qps_.size());
    for (QueuePair &qp : qps_) {
        auto send_wr_ids = runtime_->allocate(config_.qp.send_queue_depth * sizeof(std::uint64_t));
        if (!send_wr_ids)
            return unexpected(send_wr_ids.error());
        auto receive_wr_ids = runtime_->allocate(config_.qp.receive_queue_depth * sizeof(std::uint64_t));
        if (!receive_wr_ids)
            return unexpected(receive_wr_ids.error());
        send_wr_ids_.push_back(std::move(*send_wr_ids));
        receive_wr_ids_.push_back(std::move(*receive_wr_ids));
        if (const auto configured =
                qp.set_device_wr_id_storage(reinterpret_cast<std::uint64_t>(send_wr_ids_.back().data()),
                                            reinterpret_cast<std::uint64_t>(receive_wr_ids_.back().data()));
            !configured) {
            return unexpected(configured.error());
        }
    }
    return {};
}

}  // namespace nds::client
