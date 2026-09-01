#include "transport.hh"

#include "transport_protocol.hh"

#include <cstddef>
#include <algorithm>
#include <limits>
#include <utility>
#include <vector>

namespace nds::client {
namespace {

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

Transport::Transport() = default;

Transport::~Transport() = default;

Result<void> Transport::open(Runtime *runtime, const TransportConfig &config, const BackendConfig &backend) {
    if (runtime == nullptr || !runtime->initialized() || runtime_ != nullptr)
        return Error{ErrorCode::kInvalidArgument, "transport requires one open runtime"};
    if (config.qp_count == 0U || config.qp_count > nds::wire::kMaxQpInfoBatch)
        return Error{ErrorCode::kInvalidArgument, "transport QP count is outside the supported batch limit"};
    if (config.tcp_timeout_ms == 0U ||
        config.tcp_timeout_ms > static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max()))
        return Error{ErrorCode::kInvalidArgument, "transport TCP timeout is outside the supported range"};
    runtime_ = runtime;
    config_ = config;
    backend_ = backend;
    if (backend_.mode != BackendMode::Ra) {
        config_.qp.control_flags |= QueuePairCallerPollsCq;
    }
    NDS_ASSIGN_OR_RETURN(endpoint_, runtime_->create_endpoint(config_.endpoint));

    const auto server = parse_tcp_address(config_.server_address);
    if (!server.ok())
        return server.error();
    auto connected = TcpConnection::connect(server.value().ipv4, server.value().port, config_.tcp_timeout_ms);
    if (!connected.ok())
        return connected.error();
    exchange_channel_ = std::move(connected).value();
    const nds::transport::TransportInfo count_request{
        nds::transport::TransportInfoKind::QpCountRequest, config_.qp_count, {}};
    NDS_RETURN_IF_ERROR(send_transport_info(&exchange_channel_, count_request));
    const auto count_response = receive_transport_info(&exchange_channel_);
    if (!count_response.ok())
        return count_response.error();
    if (count_response.value().kind != nds::transport::TransportInfoKind::QpCountResponse ||
        count_response.value().qp_count == 0U || count_response.value().qp_count > config_.qp_count) {
        return Error{ErrorCode::kTransport, "invalid accepted QP count"};
    }
    const std::uint32_t accepted_qp_count = count_response.value().qp_count;

    qps_.reserve(accepted_qp_count);
    local_qps_.reserve(accepted_qp_count);
    for (std::uint32_t index = 0U; index < accepted_qp_count; ++index) {
        auto created = endpoint_.create_qp(config_.qp, backend_.mode);
        if (!created.ok())
            return created.error();
        qps_.push_back(std::move(created).value());
        const auto local = qps_.back().local_qp_info();
        if (!local.ok())
            return local.error();
        local_qps_.push_back(local.value());
    }
    nds::transport::TransportInfo local_info{nds::transport::TransportInfoKind::QpEndpoints, accepted_qp_count, {}};
    std::copy(local_qps_.begin(), local_qps_.end(), local_info.qps.begin());
    NDS_RETURN_IF_ERROR(send_transport_info(&exchange_channel_, local_info));
    const auto peer_info = receive_transport_info(&exchange_channel_);
    if (!peer_info.ok())
        return peer_info.error();
    if (peer_info.value().kind != nds::transport::TransportInfoKind::QpEndpoints ||
        peer_info.value().qp_count != accepted_qp_count) {
        return Error{ErrorCode::kTransport, "peer returned mismatched QP information"};
    }
    for (std::size_t index = 0U; index < qps_.size(); ++index) {
        if (const auto qp_connected = qps_[index].connect(peer_info.value().qps[index]); !qp_connected.ok())
            return qp_connected.error();
    }
    NDS_RETURN_IF_ERROR(build_device_transport());
    return ready();
}

TcpConnection *Transport::exchange_channel() noexcept {
    return &exchange_channel_;
}

const nds::QpInfo &Transport::local_qp_info() const noexcept {
    static const nds::QpInfo empty{};
    return local_qps_.empty() ? empty : local_qps_.front();
}

const std::vector<nds::QpInfo> &Transport::local_qp_infos() const noexcept {
    return local_qps_;
}

Runtime *Transport::runtime() noexcept {
    return runtime_;
}

Result<MemoryRegion> Transport::register_memory(const MemoryBuffer &buffer, MemoryAccess access) {
    if (runtime_ == nullptr || !runtime_->initialized() || !endpoint_.opened())
        return Error{ErrorCode::kInvalidArgument, "memory registration requires an open transport"};
    return endpoint_.reg_mr(buffer, access);
}

QueuePair *Transport::qp() noexcept {
    return qp(0U);
}

QueuePair *Transport::qp(std::size_t index) noexcept {
    return index < qps_.size() ? &qps_[index] : nullptr;
}

const NdsTransportDescriptor &Transport::device_transport() const noexcept {
    return device_transport_;
}

Result<NdsQpDescriptor> Transport::host_qp_descriptor(std::size_t index) const {
    if (index >= host_qp_descriptors_.size())
        return Error{ErrorCode::kInvalidArgument, "transport QP index is out of range"};
    return host_qp_descriptors_[index];
}

Result<void> Transport::build_device_transport() {
    device_transport_ = {};
    host_qp_descriptors_.clear();
    host_qp_descriptors_.reserve(qps_.size());

    for (const QueuePair &qp : qps_) {
        if (qp.backend_mode() != backend_.mode)
            return Error{ErrorCode::kRuntime, "transport QP backend mode does not match its dispatcher"};
        if (qp.backend_mode() == BackendMode::Ra) {
            NdsQpDescriptor descriptor{};
            descriptor.host_runtime_address = reinterpret_cast<std::uint64_t>(runtime_);
            descriptor.host_qp_address = reinterpret_cast<std::uint64_t>(&qp);
            host_qp_descriptors_.push_back(descriptor);
            continue;
        }
        if (qp.ai_qp_info_.ai_qp_address == 0U)
            return Error{ErrorCode::kInvalidArgument, "AI transport requires AI QPs"};
        if ((qp.backend_mode() == BackendMode::Aiv || qp.backend_mode() == BackendMode::Aicpu) &&
            (qp.send_wr_ids_.data() == nullptr || qp.receive_wr_ids_.data() == nullptr)) {
            return Error{ErrorCode::kRuntime, "AI QP is missing private WR-ID storage"};
        }

        const auto *source = reinterpret_cast<const Libra::AiDataPlaneInfo *>(qp.ai_qp_info_.data_plane_info);
        if (source == nullptr || source->send_wq.buffer_address == 0U || source->receive_wq.buffer_address == 0U)
            return Error{ErrorCode::kRa, "HCCP did not return SQ/RQ dataplane information"};

        const auto copy_wq = [](const Libra::AiDataPlaneWq &input, std::uint64_t wr_id_address, bool is_send) {
            NdsWorkQueueDescriptor output{};
            output.number = input.wqn;
            output.depth = input.depth;
            output.entry_size = input.wqebb_size;
            output.buffer_address = input.buffer_address;
            output.head_address = input.head_address;
            output.tail_address = input.tail_address;
            output.wr_id_address = wr_id_address;
            output.doorbell_mode = is_send ? NDS_DOORBELL_MMIO : NDS_DOORBELL_RECORD;
            output.doorbell_address = is_send ? input.doorbell_register_address : input.software_doorbell_address;
            return output;
        };
        const auto copy_cq = [](const Libra::AiDataPlaneCq &input) {
            NdsCqDescriptor output{};
            output.number = input.cqn;
            output.depth = input.depth;
            output.entry_size = input.cqe_size;
            output.buffer_address = input.buffer_address;
            output.consumer_address = input.tail_address;
            output.doorbell_mode = NDS_DOORBELL_RECORD;
            output.doorbell_address = input.software_doorbell_address;
            return output;
        };

        NdsQpDescriptor descriptor{};
        descriptor.flags = (qp.config_.control_flags & QueuePairCallerPollsCq) != 0U
                               ? static_cast<std::uint32_t>(NDS_QP_CALLER_POLLS_CQ)
                               : 0U;
        descriptor.qp_mode = qp.config_.ai_qp_mode;
        descriptor.service_level = qp.config_.service_level;
        descriptor.provider_qp_address = qp.ai_qp_info_.ai_qp_address;
        descriptor.provider_send_cq_address = qp.ai_qp_info_.ai_scq_address;
        descriptor.provider_receive_cq_address = qp.ai_qp_info_.ai_rcq_address;
        // AI device code does not dereference host-side handles. Keep those
        // fields zero in the device descriptor, while the host mirror remains
        // usable for host-side CQ polling.
        const std::uint64_t send_wr_ids =
            (qp.backend_mode() == BackendMode::Aiv || qp.backend_mode() == BackendMode::Aicpu)
                ? reinterpret_cast<std::uint64_t>(qp.send_wr_ids_.data())
                : 0U;
        const std::uint64_t receive_wr_ids =
            (qp.backend_mode() == BackendMode::Aiv || qp.backend_mode() == BackendMode::Aicpu)
                ? reinterpret_cast<std::uint64_t>(qp.receive_wr_ids_.data())
                : 0U;
        descriptor.send_queue = copy_wq(source->send_wq, send_wr_ids, true);
        descriptor.receive_queue = copy_wq(source->receive_wq, receive_wr_ids, false);
        descriptor.send_cq = copy_cq(source->send_cq);
        descriptor.receive_cq = copy_cq(source->receive_cq);
        host_qp_descriptors_.push_back(descriptor);
    }

    if (host_qp_descriptors_.empty())
        return Error{ErrorCode::kInvalidArgument, "device transport requires at least one QP"};

    std::vector<NdsTransportQpState> initial_states;
    initial_states.reserve(host_qp_descriptors_.size());
    for (const NdsQpDescriptor &descriptor : host_qp_descriptors_) {
        const std::uint32_t send_depth =
            descriptor.send_queue.depth != 0U ? descriptor.send_queue.depth : config_.qp.send_queue_depth;
        const std::uint32_t receive_depth =
            descriptor.receive_queue.depth != 0U ? descriptor.receive_queue.depth : config_.qp.receive_queue_depth;
        if (send_depth == 0U || receive_depth == 0U)
            return Error{ErrorCode::kInvalidArgument, "transport QP state requires nonzero queue capacities"};
        initial_states.push_back({1U, 0U, send_depth, receive_depth});
    }
    const auto state_location = backend_.mode == BackendMode::Ra ? MemoryLocation::Host : MemoryLocation::Device;
    auto state_storage = runtime_->allocate(initial_states.size() * sizeof(NdsTransportQpState), state_location);
    if (!state_storage.ok())
        return state_storage.error();
    if (const Result<void> copied = runtime_->copy_to(&state_storage.value(), initial_states.data(),
                                                      initial_states.size() * sizeof(NdsTransportQpState));
        !copied.ok()) {
        return copied.error();
    }
    qp_states_ = std::move(state_storage.value());
    device_transport_.qp_states_address = reinterpret_cast<std::uint64_t>(qp_states_.data());

    if (backend_.mode == BackendMode::Ra) {
        device_qp_addresses_ = {};
        device_transport_.qp_descriptors_address = reinterpret_cast<std::uint64_t>(host_qp_descriptors_.data());
    } else {
        auto descriptor_storage =
            runtime_->allocate(host_qp_descriptors_.size() * sizeof(NdsQpDescriptor), MemoryLocation::Device);
        if (!descriptor_storage.ok())
            return descriptor_storage.error();
        if (const Result<void> copied = runtime_->copy_to(&descriptor_storage.value(), host_qp_descriptors_.data(),
                                                          host_qp_descriptors_.size() * sizeof(NdsQpDescriptor));
            !copied.ok()) {
            return copied.error();
        }
        device_qp_addresses_ = std::move(descriptor_storage.value());
        device_transport_.qp_descriptors_address = reinterpret_cast<std::uint64_t>(device_qp_addresses_.data());
    }
    device_transport_.qp_count = static_cast<std::uint32_t>(host_qp_descriptors_.size());
    return {};
}

std::size_t Transport::qp_count() const noexcept {
    return qps_.size();
}

const BackendConfig &Transport::backend() const noexcept {
    return backend_;
}

Result<void> Transport::ready() {
    if (qps_.empty())
        return Error{ErrorCode::kInvalidArgument, "transport has no QPs"};
    for (QueuePair &qp : qps_) {
        const auto port = qp.query_port_status();
        if (!port.ok())
            return port.error();
        const auto qp_status = qp.query_status();
        if (!qp_status.ok())
            return qp_status.error();
        const auto lite = qp.query_support_lite();
        if (!lite.ok())
            return lite.error();
        if (port.value() != Libra::PORT_STATUS_ACTIVE || qp_status.value() != Libra::QP_STATUS_CONNECTED ||
            lite.value() == Libra::LITE_NOT_SUPPORTED) {
            return Error{ErrorCode::kRa, "client transport is not ready"};
        }
    }
    return {};
}

}  // namespace nds::client
