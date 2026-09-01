#include "transport.hh"

#include "transport_protocol.hh"

#include "backends/launcher.hh"
#include <cstddef>
#include <chrono>
#include <algorithm>
#include <limits>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace nds::client {
namespace {

constexpr std::uint32_t kTransportLaunchTimeoutMs = 5000U;
const LaunchConfig kLegacyTransportLaunchConfig{};

enum class SendOperation { Send, Read, Write };

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
        return received.error();
    nds::transport::TransportInfo info{};
    if (nds::transport::decode(&encoded, &info) != nds::transport::CodecResult::Ok)
        return Error{ErrorCode::kTransport, "invalid transport information"};
    return info;
}

Result<void> launch_send(Runtime *runtime, Launcher *launcher, const NdsDeviceQp &device_qp, const NdsDeviceSendWr &wr,
                         SendOperation operation) {
    if (runtime == nullptr || launcher == nullptr)
        return Error{ErrorCode::kInvalidArgument, "transport requires a runtime and backend launcher"};
    switch (operation) {
        case SendOperation::Send:
            return launcher->with_config(kLegacyTransportLaunchConfig).post_send(device_qp, wr);
        case SendOperation::Read:
            return launcher->with_config(kLegacyTransportLaunchConfig).post_send(device_qp, wr);
        case SendOperation::Write:
            return launcher->with_config(kLegacyTransportLaunchConfig).post_send(device_qp, wr);
    }
    return Error{ErrorCode::kInvalidArgument, "invalid transport send operation"};
}

Result<void> launch_receive(Runtime *runtime, Launcher *launcher, const NdsDeviceQp &device_qp,
                            const NdsDeviceRecvWr &wr) {
    if (runtime == nullptr || launcher == nullptr)
        return Error{ErrorCode::kInvalidArgument, "transport requires a runtime and backend launcher"};
    return launcher->with_config(kLegacyTransportLaunchConfig).post_recv(device_qp, wr);
}

Result<void> launch_send_batch(Runtime *runtime, Launcher *launcher, const NdsDeviceQp &device_qp,
                               std::span<const NdsDeviceSendWr> wrs) {
    if (runtime == nullptr || launcher == nullptr || wrs.empty())
        return Error{ErrorCode::kInvalidArgument, "AIV transport batch requires work requests"};
    for (const NdsDeviceSendWr &wr : wrs)
        NDS_RETURN_IF_ERROR(launcher->with_config(kLegacyTransportLaunchConfig).post_send(device_qp, wr));
    return {};
}

Result<void> launch_poll(Runtime *runtime, Launcher *launcher, const NdsDeviceQp &device_qp, bool send_cq) {
    if (runtime == nullptr || launcher == nullptr)
        return Error{ErrorCode::kInvalidArgument, "transport requires a runtime and backend launcher"};
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(kTransportLaunchTimeoutMs);
    while (std::chrono::steady_clock::now() < deadline) {
        NdsDeviceWc completion{};
        NDS_ASSIGN_OR_RETURN(
            std::uint32_t completion_count,
            launcher->with_config(kLegacyTransportLaunchConfig).poll_cq(device_qp, send_cq, 1U, &completion));
        if (completion_count != 0U)
            return completion.status == NDS_DEVICE_WC_SUCCESS ? Result<void>{}
                                                              : Error{ErrorCode::kRa, "transport completion failed"};
        std::this_thread::yield();
    }
    return Error{ErrorCode::kRuntime, "timed out waiting for transport CQ completion"};
}

}  // namespace

bool QueueHandle::valid() const noexcept {
    return owner_ != nullptr && index_ != static_cast<std::size_t>(-1);
}

Transport::Transport() = default;

Transport::~Transport() = default;

Result<void> Transport::open(Runtime *runtime, const TransportConfig &config, const BackendConfig &backend) {
    if (runtime == nullptr || !runtime->initialized() || runtime_ != nullptr)
        return Error{ErrorCode::kInvalidArgument, "transport requires one open runtime"};
    if (config.qp_count == 0U || config.qp_count > nds::wire::kMaxQpInfoBatch)
        return Error{ErrorCode::kInvalidArgument, "transport QP count is outside the supported batch limit"};
    runtime_ = runtime;
    config_ = config;
    backend_ = backend;
    if (backend_.mode != BackendMode::Ra)
        config_.qp.control_flags |= QueuePairCallerPollsCq;
    if (const auto launcher = initialize_launcher(); !launcher)
        return launcher.error();
    NDS_ASSIGN_OR_RETURN(endpoint_, runtime_->create_endpoint(config_.endpoint));

    const auto server = parse_tcp_address(config_.server_address);
    if (!server)
        return server.error();
    auto connected = TcpConnection::connect(server.value().ipv4, server.value().port, config_.tcp_timeout_ms);
    if (!connected)
        return connected.error();
    exchange_channel_ = std::move(connected).value();
    const nds::transport::TransportInfo count_request{
        nds::transport::TransportInfoKind::QpCountRequest, config_.qp_count, {}};
    NDS_RETURN_IF_ERROR(send_transport_info(&exchange_channel_, count_request));
    const auto count_response = receive_transport_info(&exchange_channel_);
    if (!count_response)
        return count_response.error();
    if (count_response.value().kind != nds::transport::TransportInfoKind::QpCountResponse ||
        count_response.value().qp_count > config_.qp_count) {
        return Error{ErrorCode::kTransport, "invalid accepted QP count"};
    }
    const std::uint32_t accepted_qp_count = count_response.value().qp_count;

    qps_.reserve(accepted_qp_count);
    local_qps_.reserve(accepted_qp_count);
    next_wr_ids_.reserve(accepted_qp_count);
    for (std::uint32_t index = 0U; index < accepted_qp_count; ++index) {
        auto created = endpoint_.create_qp(config_.qp, backend_.mode);
        if (!created)
            return created.error();
        qps_.push_back(std::move(created).value());
        next_wr_ids_.push_back(1U);
        const auto local = qps_.back().local_qp_info();
        if (!local)
            return local.error();
        local_qps_.push_back(local.value());
    }
    nds::transport::TransportInfo local_info{nds::transport::TransportInfoKind::QpEndpoints, accepted_qp_count, {}};
    std::copy(local_qps_.begin(), local_qps_.end(), local_info.qps.begin());
    NDS_RETURN_IF_ERROR(send_transport_info(&exchange_channel_, local_info));
    const auto peer_info = receive_transport_info(&exchange_channel_);
    if (!peer_info)
        return peer_info.error();
    if (peer_info.value().kind != nds::transport::TransportInfoKind::QpEndpoints ||
        peer_info.value().qp_count != accepted_qp_count) {
        return Error{ErrorCode::kTransport, "peer returned mismatched QP information"};
    }
    for (std::size_t index = 0U; index < qps_.size(); ++index) {
        if (const auto qp_connected = qps_[index].connect(peer_info.value().qps[index]); !qp_connected)
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
    if (runtime_ == nullptr || !endpoint_.opened())
        return Error{ErrorCode::kInvalidArgument, "memory registration requires an open transport"};
    return endpoint_.reg_mr(buffer, access);
}

Result<QueueHandle> Transport::queue(std::size_t index) const {
    if (index >= qps_.size())
        return Error{ErrorCode::kInvalidArgument, "transport queue index is out of range"};
    return QueueHandle(this, index);
}

QueuePair *Transport::qp() noexcept {
    return qp(0U);
}

QueuePair *Transport::qp(std::size_t index) noexcept {
    return index < qps_.size() ? &qps_[index] : nullptr;
}

QueuePair *Transport::queue_pair(QueueHandle queue) noexcept {
    return queue.valid() && queue.owner_ == this ? qp(queue.index_) : nullptr;
}

const NdsDeviceQp *Transport::device_qp(QueueHandle queue) const noexcept {
    if (!queue.valid() || queue.owner_ != this || queue.index_ >= device_qps_.size())
        return nullptr;
    return &device_qps_[queue.index_];
}

const NdsDeviceTransport &Transport::device_transport() const noexcept {
    return device_transport_;
}

Result<void> Transport::build_device_transport() {
    device_qps_.clear();
    device_qps_.reserve(qps_.size());

    for (const QueuePair &qp : qps_) {
        if (qp.backend_mode() != backend_.mode)
            return Error{ErrorCode::kRuntime, "transport QP backend mode does not match its dispatcher"};
        if (qp.backend_mode() == BackendMode::Ra) {
            NdsDeviceQp descriptor{};
            descriptor.host_runtime_address = reinterpret_cast<std::uint64_t>(runtime_);
            descriptor.host_qp_address = reinterpret_cast<std::uint64_t>(&qp);
            device_qps_.push_back(descriptor);
            continue;
        }
        if (qp.ai_qp_info_.ai_qp_address == 0U)
            return Error{ErrorCode::kInvalidArgument, "AI transport requires AI QPs"};
        if ((qp.backend_mode() == BackendMode::Aiv || qp.backend_mode() == BackendMode::Aicpu) &&
            (qp.send_wr_ids_.data() == nullptr || qp.receive_wr_ids_.data() == nullptr)) {
            return Error{ErrorCode::kRuntime, "AI QP is missing private WR-ID storage"};
        }

        const auto *source = reinterpret_cast<const Libra::AiDataPlaneInfo *>(qp.ai_qp_info_.data_plane_info);
        if (source->send_wq.buffer_address == 0U || source->receive_wq.buffer_address == 0U)
            return Error{ErrorCode::kRa, "HCCP did not return SQ/RQ dataplane information"};

        const auto copy_wq = [](const Libra::AiDataPlaneWq &input, std::uint64_t wr_id_address, bool is_send) {
            NdsDeviceWorkQueue output{};
            output.number = input.wqn;
            output.depth = input.depth;
            output.entry_size = input.wqebb_size;
            output.buffer_address = input.buffer_address;
            output.head_address = input.head_address;
            output.tail_address = input.tail_address;
            output.wr_id_address = wr_id_address;
            output.doorbell_mode = is_send ? NDS_DEVICE_DOORBELL_MMIO : NDS_DEVICE_DOORBELL_RECORD;
            output.doorbell_address = is_send ? input.doorbell_register_address : input.software_doorbell_address;
            return output;
        };
        const auto copy_cq = [](const Libra::AiDataPlaneCq &input) {
            NdsDeviceCq output{};
            output.number = input.cqn;
            output.depth = input.depth;
            output.entry_size = input.cqe_size;
            output.buffer_address = input.buffer_address;
            output.consumer_address = input.tail_address;
            output.doorbell_mode = NDS_DEVICE_DOORBELL_RECORD;
            output.doorbell_address = input.software_doorbell_address;
            return output;
        };

        NdsDeviceQp descriptor{};
        descriptor.flags = (qp.config_.control_flags & QueuePairCallerPollsCq) != 0U
                               ? static_cast<std::uint32_t>(NDS_DEVICE_QP_CALLER_POLLS_CQ)
                               : 0U;
        descriptor.qp_mode = qp.config_.ai_qp_mode;
        descriptor.service_level = qp.config_.service_level;
        descriptor.provider_qp_address = qp.ai_qp_info_.ai_qp_address;
        descriptor.provider_send_cq_address = qp.ai_qp_info_.ai_scq_address;
        descriptor.provider_receive_cq_address = qp.ai_qp_info_.ai_rcq_address;
        descriptor.host_runtime_address = reinterpret_cast<std::uint64_t>(runtime_);
        descriptor.host_qp_address = reinterpret_cast<std::uint64_t>(&qp);
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
        device_qps_.push_back(descriptor);
    }

    if (device_qps_.empty())
        return Error{ErrorCode::kInvalidArgument, "device transport requires at least one QP"};

    auto descriptor_storage = runtime_->allocate(device_qps_.size() * sizeof(NdsDeviceQp), MemoryLocation::Device);
    if (!descriptor_storage.ok())
        return descriptor_storage.error();
    if (const Result<void> copied = runtime_->copy_to(&descriptor_storage.value(), device_qps_.data(),
                                                      device_qps_.size() * sizeof(NdsDeviceQp));
        !copied.ok()) {
        return copied.error();
    }
    device_qp_storage_ = std::move(descriptor_storage.value());
    device_transport_.qp_descriptors_address = reinterpret_cast<std::uint64_t>(device_qp_storage_.data());
    device_transport_.qp_count = static_cast<std::uint32_t>(device_qps_.size());
    return {};
}

Result<void> Transport::send(QueueHandle queue, const TransportSend &request) {
    const SendRequest submission{request.local, request.length, request.local_offset, nullptr};
    return submit_sends(queue, std::span{&submission, 1U}, NDS_DEVICE_WR_SEND);
}

Result<void> Transport::receive(QueueHandle queue, const TransportReceive &request) {
    return submit_receive(queue, request);
}

Result<void> Transport::read(QueueHandle queue, const TransportRead &request) {
    const SendRequest submission{request.local, request.length, request.local_offset, &request.remote};
    return submit_sends(queue, std::span{&submission, 1U}, NDS_DEVICE_WR_RDMA_READ);
}

Result<void> Transport::write(QueueHandle queue, const TransportWrite &request) {
    const SendRequest submission{request.local, request.length, request.local_offset, &request.remote};
    return submit_sends(queue, std::span{&submission, 1U}, NDS_DEVICE_WR_RDMA_WRITE);
}

Result<void> Transport::wait_receive(QueueHandle queue) {
    return complete(queue_pair(queue), false);
}

Result<void> Transport::send_batch(QueueHandle queue, std::span<const TransportSend> requests) {
    if (requests.empty())
        return Error{ErrorCode::kInvalidArgument, "transport send batch requires at least one request"};
    std::vector<SendRequest> submissions;
    submissions.reserve(requests.size());
    for (const TransportSend &request : requests)
        submissions.push_back({request.local, request.length, request.local_offset, nullptr});
    return submit_sends(queue, submissions, NDS_DEVICE_WR_SEND);
}

Result<void> Transport::receive_batch(QueueHandle queue, std::span<const TransportReceive> requests) {
    if (requests.empty())
        return Error{ErrorCode::kInvalidArgument, "transport receive batch requires at least one request"};
    if (requests.size() != 1U)
        return Error{ErrorCode::kUnsupported, "multi-request transport receive batches are unavailable"};
    return receive(queue, requests.front());
}

Result<void> Transport::read_batch(QueueHandle queue, std::span<const TransportRead> requests) {
    if (requests.empty())
        return Error{ErrorCode::kInvalidArgument, "transport read batch requires at least one request"};
    std::vector<SendRequest> submissions;
    submissions.reserve(requests.size());
    for (const TransportRead &request : requests)
        submissions.push_back({request.local, request.length, request.local_offset, &request.remote});
    return submit_sends(queue, submissions, NDS_DEVICE_WR_RDMA_READ);
}

Result<void> Transport::write_batch(QueueHandle queue, std::span<const TransportWrite> requests) {
    if (requests.empty())
        return Error{ErrorCode::kInvalidArgument, "transport write batch requires at least one request"};
    std::vector<SendRequest> submissions;
    submissions.reserve(requests.size());
    for (const TransportWrite &request : requests)
        submissions.push_back({request.local, request.length, request.local_offset, &request.remote});
    return submit_sends(queue, submissions, NDS_DEVICE_WR_RDMA_WRITE);
}

Result<void> Transport::submit_sends(QueueHandle queue, std::span<const SendRequest> requests, std::uint32_t opcode) {
    QueuePair *const qp = queue_pair(queue);
    if (runtime_ == nullptr || qp == nullptr || requests.empty()) {
        return Error{ErrorCode::kInvalidArgument,
                     "transport send requires a queue and at least one valid local memory range"};
    }
    const std::size_t index = queue.index_;
    std::vector<NdsDeviceSendWr> wrs;
    wrs.reserve(requests.size());
    for (std::size_t request_index = 0U; request_index < requests.size(); ++request_index) {
        const SendRequest &request = requests[request_index];
        if (request.local == nullptr || !request.local->belongs_to(&endpoint_) || request.length == 0U ||
            request.local_offset > request.local->length() ||
            request.length > request.local->length() - request.local_offset ||
            ((request.remote == nullptr) != (opcode == NDS_DEVICE_WR_SEND)) ||
            (request.remote != nullptr &&
             (request.remote->address == 0U || request.remote->key == 0U || request.remote->length < request.length))) {
            return Error{ErrorCode::kInvalidArgument, "transport request has invalid local or remote memory"};
        }
        wrs.push_back(
            {next_wr_ids_[index]++,
             opcode,
             request_index + 1U == requests.size() ? static_cast<std::uint32_t>(NDS_DEVICE_SEND_SIGNALED) : 0U,
             {request.local->address() + request.local_offset, request.length, request.local->local_key()},
             request.remote == nullptr ? 0U : request.remote->address,
             request.remote == nullptr ? 0U : request.remote->key,
             0U});
        if (next_wr_ids_[index] == 0U)
            ++next_wr_ids_[index];
    }
    if (backend_.mode != BackendMode::Aiv && wrs.size() != 1U)
        return Error{ErrorCode::kUnsupported, "only the AIV backend currently supports transport send batches"};
    const NdsDeviceQp *const descriptor = device_qp(queue);
    if (descriptor == nullptr)
        return Error{ErrorCode::kRuntime, "transport is missing its device QP descriptor"};
    if (backend_.mode == BackendMode::Aiv) {
        if (const auto submitted = launch_send_batch(runtime_, backend_launcher_.get(), *descriptor, wrs); !submitted)
            return Error{submitted.error()};
    } else {
        const SendOperation operation = opcode == NDS_DEVICE_WR_SEND        ? SendOperation::Send
                                        : opcode == NDS_DEVICE_WR_RDMA_READ ? SendOperation::Read
                                                                            : SendOperation::Write;
        if (const auto submitted = launch_send(runtime_, backend_launcher_.get(), *descriptor, wrs.front(), operation);
            !submitted) {
            return Error{submitted.error()};
        }
    }
    return complete(qp, true);
}

Result<void> Transport::submit_receive(QueueHandle queue, const TransportReceive &request) {
    QueuePair *const qp = queue_pair(queue);
    if (qp == nullptr || request.local == nullptr || !request.local->belongs_to(&endpoint_) || request.length == 0U ||
        request.local_offset > request.local->length() ||
        request.length > request.local->length() - request.local_offset) {
        return Error{ErrorCode::kInvalidArgument, "transport receive requires a queue and a valid local memory range"};
    }
    const std::size_t index = queue.index_;
    NdsDeviceRecvWr recv{next_wr_ids_[index]++,
                         {request.local->address() + request.local_offset, request.length, request.local->local_key()}};
    if (next_wr_ids_[index] == 0U)
        ++next_wr_ids_[index];
    const NdsDeviceQp *const descriptor = device_qp(queue);
    if (descriptor == nullptr)
        return Error{ErrorCode::kRuntime, "transport is missing its device QP descriptor"};
    return launch_receive(runtime_, backend_launcher_.get(), *descriptor, recv);
}

Result<void> Transport::complete(QueuePair *qp, bool send_cq) {
    if (qp == nullptr)
        return Error{ErrorCode::kInvalidArgument, "transport completion requires a queue"};
    const std::size_t index = static_cast<std::size_t>(qp - qps_.data());
    const NdsDeviceQp *const descriptor = index < device_qps_.size() ? &device_qps_[index] : nullptr;
    if (descriptor == nullptr)
        return Error{ErrorCode::kRuntime, "transport is missing its device QP descriptor"};
    return launch_poll(runtime_, backend_launcher_.get(), *descriptor, send_cq);
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
        if (!port)
            return port.error();
        const auto qp_status = qp.query_status();
        if (!qp_status)
            return qp_status.error();
        const auto lite = qp.query_support_lite();
        if (!lite)
            return lite.error();
        if (port.value() != Libra::PORT_STATUS_ACTIVE || qp_status.value() != Libra::QP_STATUS_CONNECTED ||
            lite.value() == Libra::LITE_NOT_SUPPORTED) {
            return Error{ErrorCode::kRa, "client transport is not ready"};
        }
    }
    return {};
}

Result<void> Transport::initialize_launcher() {
    NDS_ASSIGN_OR_RETURN(backend_launcher_, Launcher::open(runtime_, backend_.mode, backend_.artifact_path));
    return {};
}

}  // namespace nds::client
