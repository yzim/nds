#include "transport.hh"

#include <utility>

namespace nds::client {

Result<void> Transport::open(Runtime *runtime, const TransportConfig &config, const ExecutionConfig &execution) {
    if (runtime == nullptr || !runtime->initialized() || runtime_ != nullptr)
        return unexpected(ErrorCode::kInvalidArgument, "transport requires one open runtime");
    runtime_ = runtime;
    config_ = config;
    execution_ = execution;
    if (const auto opened = endpoint_.open(runtime_, config_.endpoint); !opened)
        return unexpected(opened.error());
    auto created = endpoint_.create_qp(config_.qp, execution_.mode);
    if (!created)
        return unexpected(created.error());
    qp_ = std::move(*created);
    const auto local = qp_.local_qp_info();
    if (!local)
        return unexpected(local.error());
    local_ = *local;
    if (const auto private_memory = initialize_private_memory(); !private_memory)
        return unexpected(private_memory.error());
    if (const auto connected =
            TcpPeerExchange::connect(config_.cpu_ipv4, config_.tcp_port, config_.tcp_timeout_ms, &bootstrap_);
        !connected) {
        return unexpected(connected.error());
    }
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

const nds_qp_info &Transport::local_qp_info() const noexcept {
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

const ExecutionConfig &Transport::execution() const noexcept {
    return execution_;
}

Result<void> Transport::ready() {
    int port = -1;
    int qp_status = -1;
    int lite = -1;
    if (const auto queried = qp_.query_port_status(&port); !queried)
        return unexpected(queried.error());
    if (const auto queried = qp_.query_status(&qp_status); !queried)
        return unexpected(queried.error());
    if (const auto queried = qp_.query_support_lite(&lite); !queried)
        return unexpected(queried.error());
    if (port != NDS_RA_PORT_STATUS_ACTIVE || qp_status != NDS_RA_QP_STATUS_CONNECTED ||
        lite == NDS_RA_LITE_NOT_SUPPORTED) {
        return unexpected(ErrorCode::kRa, "client transport is not ready");
    }
    return {};
}

Result<void> Transport::initialize_private_memory() {
    if (execution_.mode == NpuExecutionMode::Ra)
        return {};
    Memory *memory = runtime_->memory();
    if (const auto allocated = memory->allocate(config_.qp.send_queue_depth * sizeof(std::uint64_t), &send_wr_ids_);
        !allocated) {
        return unexpected(allocated.error());
    }
    if (const auto allocated =
            memory->allocate(config_.qp.receive_queue_depth * sizeof(std::uint64_t), &receive_wr_ids_);
        !allocated) {
        return unexpected(allocated.error());
    }
    return qp_.set_device_wr_id_storage(reinterpret_cast<std::uint64_t>(send_wr_ids_.data()),
                                        reinterpret_cast<std::uint64_t>(receive_wr_ids_.data()));
}

}  // namespace nds::client
