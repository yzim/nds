#include "transport.hh"

#include "transport_protocol.hh"

#include "launcher.hh"
#include "ra/ra.hh"

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

enum class SendOperation { Send, Read, Write };

struct AivThreeAddressArguments {
    std::uint64_t first_address;
    std::uint64_t second_address;
    std::uint64_t return_value_address;
};

struct AivBatchArguments {
    std::uint64_t qp_address;
    std::uint64_t wrs_address;
    std::uint32_t wr_count;
    std::uint32_t reserved;
    std::uint64_t bad_wr_address;
    std::uint64_t return_value_address;
};

struct AivPollArguments {
    std::uint64_t qp_address;
    std::uint32_t is_send_cq;
    std::uint32_t max_completions;
    std::uint64_t wc_address;
    std::uint64_t return_value_address;
};

Result<void> send_transport_info(TcpConnection *channel, const nds::transport::TransportInfo &info) {
    if (channel == nullptr)
        return unexpected(ErrorCode::kInvalidArgument, "TCP connection is required");
    nds::wire::TransportInfo encoded{};
    if (nds::transport::encode(&info, &encoded) != nds::transport::CodecResult::Ok)
        return unexpected(ErrorCode::kTransport, "cannot encode transport information");
    return channel->send(std::as_bytes(std::span{&encoded, 1U}));
}

Result<nds::transport::TransportInfo> receive_transport_info(TcpConnection *channel) {
    if (channel == nullptr)
        return unexpected(ErrorCode::kInvalidArgument, "TCP connection is required");
    nds::wire::TransportInfo encoded{};
    if (const auto received = channel->receive(std::as_writable_bytes(std::span{&encoded, 1U})); !received)
        return unexpected(received.error());
    nds::transport::TransportInfo info{};
    if (nds::transport::decode(&encoded, &info) != nds::transport::CodecResult::Ok)
        return unexpected(ErrorCode::kTransport, "invalid transport information");
    return info;
}

Result<void> launch_send(Runtime *runtime, const BackendConfig &backend, AivLauncher *aiv, AicpuLauncher *aicpu,
                         const NdsDeviceTransport &device_transport, const NdsDeviceSendWr &wr,
                         SendOperation operation) {
    if (backend.mode == NpuBackend::Ra) {
        return unexpected(ErrorCode::kInvalidArgument, "RA transport submissions require an RA queue");
    }
    const char *const aicpu_kernel = operation == SendOperation::Send   ? "nds_aicpu_rdma_send_kernel"
                                     : operation == SendOperation::Read ? "nds_aicpu_rdma_read_kernel"
                                                                        : "nds_aicpu_rdma_write_kernel";
    const char *const aiv_kernel = operation == SendOperation::Send   ? "nds_aiv_rdma_send_kernel"
                                   : operation == SendOperation::Read ? "nds_aiv_rdma_read_kernel"
                                                                      : "nds_aiv_rdma_write_kernel";
    if (backend.mode == NpuBackend::Aicpu) {
        NdsDeviceRdmaSendArgs request{device_transport, wr, std::numeric_limits<std::int32_t>::min()};
        auto request_buffer = runtime->allocate(sizeof(request));
        if (!request_buffer)
            return unexpected(request_buffer.error());
        if (const auto copied = runtime->copy_to(&*request_buffer, &request, sizeof(request)); !copied)
            return unexpected(copied.error());
        if (aicpu == nullptr)
            return unexpected(ErrorCode::kRuntime, "AICPU transport launcher is unavailable");
        if (const auto launched = aicpu->launch_and_wait(
                aicpu_kernel, reinterpret_cast<std::uint64_t>(request_buffer->data()), kTransportLaunchTimeoutMs);
            !launched) {
            return unexpected(launched.error());
        }
        if (const auto copied = runtime->copy_from(&request, *request_buffer, sizeof(request)); !copied)
            return unexpected(copied.error());
        if (request.return_value != 0)
            return unexpected(ErrorCode::kRuntime,
                              "device transport send failed: " + std::to_string(request.return_value));
        return {};
    }

    NdsDeviceRdmaSendArgs request{device_transport, wr, std::numeric_limits<std::int32_t>::min()};
    auto request_buffer = runtime->allocate(sizeof(request));
    if (!request_buffer)
        return unexpected(request_buffer.error());
    if (const auto copied = runtime->copy_to(&*request_buffer, &request, sizeof(request)); !copied)
        return unexpected(copied.error());
    if (aiv == nullptr)
        return unexpected(ErrorCode::kRuntime, "AIV transport launcher is unavailable");
    const std::uint64_t request_address = reinterpret_cast<std::uint64_t>(request_buffer->data());
    AivThreeAddressArguments arguments{request_address + offsetof(NdsDeviceRdmaSendArgs, transport),
                                       request_address + offsetof(NdsDeviceRdmaSendArgs, wr),
                                       request_address + offsetof(NdsDeviceRdmaSendArgs, return_value)};
    if (const auto launched =
            aiv->launch_and_wait(aiv_kernel, &arguments, sizeof(arguments), kTransportLaunchTimeoutMs);
        !launched) {
        return unexpected(launched.error());
    }
    if (const auto copied = runtime->copy_from(&request, *request_buffer, sizeof(request)); !copied)
        return unexpected(copied.error());
    if (request.return_value != 0)
        return unexpected(ErrorCode::kRuntime, "device transport send failed: " + std::to_string(request.return_value));
    return {};
}

Result<void> launch_receive(Runtime *runtime, const BackendConfig &backend, AivLauncher *aiv, AicpuLauncher *aicpu,
                            const NdsDeviceTransport &device_transport, const NdsDeviceRecvWr &wr) {
    if (backend.mode == NpuBackend::Aicpu) {
        NdsDeviceRdmaRecvArgs request{device_transport, wr, std::numeric_limits<std::int32_t>::min()};
        auto request_buffer = runtime->allocate(sizeof(request));
        if (!request_buffer)
            return unexpected(request_buffer.error());
        if (const auto copied = runtime->copy_to(&*request_buffer, &request, sizeof(request)); !copied)
            return unexpected(copied.error());
        if (aicpu == nullptr)
            return unexpected(ErrorCode::kRuntime, "AICPU transport launcher is unavailable");
        if (const auto launched = aicpu->launch_and_wait("nds_aicpu_rdma_recv_kernel",
                                                         reinterpret_cast<std::uint64_t>(request_buffer->data()),
                                                         kTransportLaunchTimeoutMs);
            !launched) {
            return unexpected(launched.error());
        }
        if (const auto copied = runtime->copy_from(&request, *request_buffer, sizeof(request)); !copied)
            return unexpected(copied.error());
        if (request.return_value != 0)
            return unexpected(ErrorCode::kRuntime,
                              "device transport receive failed: " + std::to_string(request.return_value));
        return {};
    }
    NdsDeviceRdmaRecvArgs request{device_transport, wr, std::numeric_limits<std::int32_t>::min()};
    auto request_buffer = runtime->allocate(sizeof(request));
    if (!request_buffer)
        return unexpected(request_buffer.error());
    if (const auto copied = runtime->copy_to(&*request_buffer, &request, sizeof(request)); !copied)
        return unexpected(copied.error());
    if (aiv == nullptr)
        return unexpected(ErrorCode::kRuntime, "AIV transport launcher is unavailable");
    const std::uint64_t request_address = reinterpret_cast<std::uint64_t>(request_buffer->data());
    AivThreeAddressArguments arguments{request_address + offsetof(NdsDeviceRdmaRecvArgs, transport),
                                       request_address + offsetof(NdsDeviceRdmaRecvArgs, wr),
                                       request_address + offsetof(NdsDeviceRdmaRecvArgs, return_value)};
    if (const auto launched =
            aiv->launch_and_wait("nds_aiv_rdma_recv_kernel", &arguments, sizeof(arguments), kTransportLaunchTimeoutMs);
        !launched) {
        return unexpected(launched.error());
    }
    if (const auto copied = runtime->copy_from(&request, *request_buffer, sizeof(request)); !copied)
        return unexpected(copied.error());
    if (request.return_value != 0)
        return unexpected(ErrorCode::kRuntime,
                          "device transport receive failed: " + std::to_string(request.return_value));
    return {};
}

Result<void> launch_send_batch(Runtime *runtime, AivLauncher *aiv, const NdsDeviceTransport &device_transport,
                               std::span<const NdsDeviceSendWr> wrs) {
    if (aiv == nullptr || wrs.empty())
        return unexpected(ErrorCode::kInvalidArgument, "AIV transport batch requires work requests");
    auto wr_buffer = runtime->allocate(wrs.size_bytes());
    if (!wr_buffer)
        return unexpected(wr_buffer.error());
    if (const auto copied = runtime->copy_to(&*wr_buffer, wrs.data(), wrs.size_bytes()); !copied)
        return unexpected(copied.error());
    NdsDevicePostSendBatchArgs request{device_transport.control_qp, reinterpret_cast<std::uint64_t>(wr_buffer->data()),
                                       static_cast<std::uint32_t>(wrs.size()), std::numeric_limits<std::int32_t>::min(),
                                       0U};
    auto request_buffer = runtime->allocate(sizeof(request));
    if (!request_buffer)
        return unexpected(request_buffer.error());
    if (const auto copied = runtime->copy_to(&*request_buffer, &request, sizeof(request)); !copied)
        return unexpected(copied.error());
    const std::uint64_t address = reinterpret_cast<std::uint64_t>(request_buffer->data());
    AivBatchArguments arguments{address + offsetof(NdsDevicePostSendBatchArgs, qp),
                                request.wrs_address,
                                request.wr_count,
                                0U,
                                address + offsetof(NdsDevicePostSendBatchArgs, bad_wr_address),
                                address + offsetof(NdsDevicePostSendBatchArgs, return_value)};
    if (const auto launched = aiv->launch_and_wait("nds_aiv_post_send_batch_kernel", &arguments, sizeof(arguments),
                                                   kTransportLaunchTimeoutMs);
        !launched) {
        return unexpected(launched.error());
    }
    if (const auto copied = runtime->copy_from(&request, *request_buffer, sizeof(request)); !copied)
        return unexpected(copied.error());
    return request.return_value == 0
               ? Result<void>{}
               : unexpected(ErrorCode::kRuntime, "AIV transport batch failed: " + std::to_string(request.return_value));
}

Result<void> launch_poll(Runtime *runtime, const BackendConfig &backend, AivLauncher *aiv, AicpuLauncher *aicpu,
                         const NdsDeviceTransport &device_transport, bool send_cq) {
    auto wc = runtime->allocate(sizeof(NdsDeviceWc));
    if (!wc)
        return unexpected(wc.error());
    auto request_buffer = runtime->allocate(sizeof(NdsDevicePollCqArgs));
    if (!request_buffer)
        return unexpected(request_buffer.error());
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(kTransportLaunchTimeoutMs);
    while (std::chrono::steady_clock::now() < deadline) {
        NdsDevicePollCqArgs request{device_transport.control_qp, send_cq ? 1U : 0U, 1U,
                                    reinterpret_cast<std::uint64_t>(wc->data()),
                                    std::numeric_limits<std::int32_t>::min()};
        if (const auto copied = runtime->copy_to(&*request_buffer, &request, sizeof(request)); !copied)
            return unexpected(copied.error());
        if (backend.mode == NpuBackend::Aicpu) {
            if (aicpu == nullptr)
                return unexpected(ErrorCode::kRuntime, "AICPU transport launcher is unavailable");
            if (const auto launched = aicpu->launch_and_wait("nds_aicpu_poll_cq_kernel",
                                                             reinterpret_cast<std::uint64_t>(request_buffer->data()),
                                                             kTransportLaunchTimeoutMs);
                !launched) {
                return unexpected(launched.error());
            }
        } else {
            if (aiv == nullptr)
                return unexpected(ErrorCode::kRuntime, "AIV transport launcher is unavailable");
            const std::uint64_t address = reinterpret_cast<std::uint64_t>(request_buffer->data());
            AivPollArguments arguments{address + offsetof(NdsDevicePollCqArgs, qp), send_cq ? 1U : 0U, 1U,
                                       reinterpret_cast<std::uint64_t>(wc->data()),
                                       address + offsetof(NdsDevicePollCqArgs, return_value)};
            if (const auto launched = aiv->launch_and_wait("nds_aiv_poll_cq_kernel", &arguments, sizeof(arguments),
                                                           kTransportLaunchTimeoutMs);
                !launched) {
                return unexpected(launched.error());
            }
        }
        if (const auto copied = runtime->copy_from(&request, *request_buffer, sizeof(request)); !copied)
            return unexpected(copied.error());
        if (request.return_value > 0)
            return {};
        if (request.return_value < 0)
            return unexpected(ErrorCode::kRuntime, "device CQ poll failed: " + std::to_string(request.return_value));
        std::this_thread::yield();
    }
    return unexpected(ErrorCode::kRuntime, "timed out waiting for transport CQ completion");
}

}  // namespace

bool QueueHandle::valid() const noexcept {
    return owner_ != nullptr && index_ != static_cast<std::size_t>(-1);
}

Transport::Transport() = default;

Transport::~Transport() = default;

Result<void> Transport::open(Runtime *runtime, const TransportConfig &config, const BackendConfig &backend) {
    if (runtime == nullptr || !runtime->initialized() || runtime_ != nullptr)
        return unexpected(ErrorCode::kInvalidArgument, "transport requires one open runtime");
    if (config.qp_count == 0U || config.qp_count > nds::wire::kMaxQpInfoBatch)
        return unexpected(ErrorCode::kInvalidArgument, "transport QP count is outside the supported batch limit");
    runtime_ = runtime;
    config_ = config;
    backend_ = backend;
    if (backend_.mode != NpuBackend::Ra)
        config_.qp.control_flags |= QueuePairCallerPollsCq;
    if (const auto launcher = initialize_launcher(); !launcher)
        return unexpected(launcher.error());
    if (const auto opened = endpoint_.open(runtime_, config_.endpoint); !opened)
        return unexpected(opened.error());
    const auto server = parse_tcp_address(config_.server_address);
    if (!server)
        return unexpected(server.error());
    auto connected = TcpConnection::connect(server->ipv4, server->port, config_.tcp_timeout_ms);
    if (!connected)
        return unexpected(connected.error());
    exchange_channel_ = std::move(*connected);
    const nds::transport::TransportInfo count_request{
        nds::transport::TransportInfoKind::QpCountRequest, config_.qp_count, {}};
    NDS_RETURN_IF_ERROR(send_transport_info(&exchange_channel_, count_request));
    const auto count_response = receive_transport_info(&exchange_channel_);
    if (!count_response)
        return unexpected(count_response.error());
    if (count_response->kind != nds::transport::TransportInfoKind::QpCountResponse ||
        count_response->qp_count > config_.qp_count) {
        return unexpected(ErrorCode::kTransport, "invalid accepted QP count");
    }
    const std::uint32_t accepted_qp_count = count_response->qp_count;

    qps_.reserve(accepted_qp_count);
    local_qps_.reserve(accepted_qp_count);
    next_wr_ids_.reserve(accepted_qp_count);
    for (std::uint32_t index = 0U; index < accepted_qp_count; ++index) {
        auto created = endpoint_.create_qp(config_.qp, backend_.mode);
        if (!created)
            return unexpected(created.error());
        qps_.push_back(std::move(*created));
        next_wr_ids_.push_back(1U);
        const auto local = qps_.back().local_qp_info();
        if (!local)
            return unexpected(local.error());
        local_qps_.push_back(*local);
    }
    if (const auto private_memory = initialize_private_memory(); !private_memory)
        return unexpected(private_memory.error());
    nds::transport::TransportInfo local_info{nds::transport::TransportInfoKind::QpEndpoints, accepted_qp_count, {}};
    std::copy(local_qps_.begin(), local_qps_.end(), local_info.qps.begin());
    NDS_RETURN_IF_ERROR(send_transport_info(&exchange_channel_, local_info));
    const auto peer_info = receive_transport_info(&exchange_channel_);
    if (!peer_info)
        return unexpected(peer_info.error());
    if (peer_info->kind != nds::transport::TransportInfoKind::QpEndpoints || peer_info->qp_count != accepted_qp_count) {
        return unexpected(ErrorCode::kTransport, "peer returned mismatched QP information");
    }
    for (std::size_t index = 0U; index < qps_.size(); ++index) {
        if (const auto qp_connected = qps_[index].connect(peer_info->qps[index]); !qp_connected)
            return unexpected(qp_connected.error());
    }
    return ready();
}

TcpConnection *Transport::exchange_channel() noexcept {
    return &exchange_channel_;
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

Result<MemoryRegion> Transport::register_memory(const MemoryBuffer &buffer, MemoryAccess access) {
    if (runtime_ == nullptr || !endpoint_.opened())
        return unexpected(ErrorCode::kInvalidArgument, "memory registration requires an open transport");
    return endpoint_.reg_mr(buffer, access);
}

Result<QueueHandle> Transport::queue(std::size_t index) const {
    if (index >= qps_.size())
        return unexpected(ErrorCode::kInvalidArgument, "transport queue index is out of range");
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
        return unexpected(ErrorCode::kInvalidArgument, "transport send batch requires at least one request");
    std::vector<SendRequest> submissions;
    submissions.reserve(requests.size());
    for (const TransportSend &request : requests)
        submissions.push_back({request.local, request.length, request.local_offset, nullptr});
    return submit_sends(queue, submissions, NDS_DEVICE_WR_SEND);
}

Result<void> Transport::receive_batch(QueueHandle queue, std::span<const TransportReceive> requests) {
    if (requests.empty())
        return unexpected(ErrorCode::kInvalidArgument, "transport receive batch requires at least one request");
    if (requests.size() != 1U)
        return unexpected(ErrorCode::kUnsupported, "multi-request transport receive batches are unavailable");
    return receive(queue, requests.front());
}

Result<void> Transport::read_batch(QueueHandle queue, std::span<const TransportRead> requests) {
    if (requests.empty())
        return unexpected(ErrorCode::kInvalidArgument, "transport read batch requires at least one request");
    std::vector<SendRequest> submissions;
    submissions.reserve(requests.size());
    for (const TransportRead &request : requests)
        submissions.push_back({request.local, request.length, request.local_offset, &request.remote});
    return submit_sends(queue, submissions, NDS_DEVICE_WR_RDMA_READ);
}

Result<void> Transport::write_batch(QueueHandle queue, std::span<const TransportWrite> requests) {
    if (requests.empty())
        return unexpected(ErrorCode::kInvalidArgument, "transport write batch requires at least one request");
    std::vector<SendRequest> submissions;
    submissions.reserve(requests.size());
    for (const TransportWrite &request : requests)
        submissions.push_back({request.local, request.length, request.local_offset, &request.remote});
    return submit_sends(queue, submissions, NDS_DEVICE_WR_RDMA_WRITE);
}

Result<void> Transport::submit_sends(QueueHandle queue, std::span<const SendRequest> requests, std::uint32_t opcode) {
    QueuePair *const qp = queue_pair(queue);
    if (runtime_ == nullptr || qp == nullptr || requests.empty()) {
        return unexpected(ErrorCode::kInvalidArgument,
                          "transport send requires a queue and at least one valid local memory range");
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
            return unexpected(ErrorCode::kInvalidArgument, "transport request has invalid local or remote memory");
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
    if (backend_.mode == NpuBackend::Ra) {
        std::vector<NdsRaSge> sges(wrs.size());
        NdsRaSendResponse last{};
        for (std::size_t request_index = 0U; request_index < wrs.size(); ++request_index) {
            const auto prepared = NdsRaPrepareSend(qp, wrs[request_index], &sges[request_index]);
            if (!prepared)
                return unexpected(prepared.error());
            last = *prepared;
        }
        if (const auto rung = NdsRaRingSend(runtime_, last); !rung)
            return unexpected(rung.error());
        return complete(qp, true);
    }
    if (backend_.mode == NpuBackend::Aicpu && wrs.size() != 1U)
        return unexpected(ErrorCode::kUnsupported, "AICPU transport batches require a linked-provider implementation");
    const auto device_transport = qp->make_device_transport();
    if (!device_transport)
        return unexpected(device_transport.error());
    if (backend_.mode == NpuBackend::Aiv) {
        if (const auto submitted = launch_send_batch(runtime_, aiv_launcher_.get(), *device_transport, wrs); !submitted)
            return unexpected(submitted.error());
    } else {
        const SendOperation operation = opcode == NDS_DEVICE_WR_SEND        ? SendOperation::Send
                                        : opcode == NDS_DEVICE_WR_RDMA_READ ? SendOperation::Read
                                                                            : SendOperation::Write;
        if (const auto submitted = launch_send(runtime_, backend_, aiv_launcher_.get(), aicpu_launcher_.get(),
                                               *device_transport, wrs.front(), operation);
            !submitted) {
            return unexpected(submitted.error());
        }
    }
    return complete(qp, true);
}

Result<void> Transport::submit_receive(QueueHandle queue, const TransportReceive &request) {
    QueuePair *const qp = queue_pair(queue);
    if (qp == nullptr || request.local == nullptr || !request.local->belongs_to(&endpoint_) || request.length == 0U ||
        request.local_offset > request.local->length() ||
        request.length > request.local->length() - request.local_offset) {
        return unexpected(ErrorCode::kInvalidArgument,
                          "transport receive requires a queue and a valid local memory range");
    }
    const std::size_t index = queue.index_;
    NdsDeviceRecvWr recv{next_wr_ids_[index]++,
                         {request.local->address() + request.local_offset, request.length, request.local->local_key()}};
    if (next_wr_ids_[index] == 0U)
        ++next_wr_ids_[index];
    if (backend_.mode == NpuBackend::Ra)
        return NdsRaPostRecv(qp, recv);
    const auto device_transport = qp->make_device_transport();
    if (!device_transport)
        return unexpected(device_transport.error());
    return launch_receive(runtime_, backend_, aiv_launcher_.get(), aicpu_launcher_.get(), *device_transport, recv);
}

Result<void> Transport::complete(QueuePair *qp, bool send_cq) {
    if (qp == nullptr)
        return unexpected(ErrorCode::kInvalidArgument, "transport completion requires a queue");
    if (backend_.mode == NpuBackend::Ra) {
        NdsDeviceWc completion{};
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(kTransportLaunchTimeoutMs);
        while (std::chrono::steady_clock::now() < deadline) {
            const auto polled = NdsRaPollCq(qp, send_cq, 1U, &completion);
            if (!polled)
                return unexpected(polled.error());
            if (*polled != 0U)
                return completion.status == NDS_RA_WC_SUCCESS
                           ? Result<void>{}
                           : unexpected(ErrorCode::kRa, "RA transport completion failed");
            std::this_thread::yield();
        }
        return unexpected(ErrorCode::kRa, "timed out waiting for RA transport completion");
    }
    const auto device_transport = qp->make_device_transport();
    if (!device_transport)
        return unexpected(device_transport.error());
    return launch_poll(runtime_, backend_, aiv_launcher_.get(), aicpu_launcher_.get(), *device_transport, send_cq);
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

Result<void> Transport::initialize_launcher() {
    if (backend_.mode == NpuBackend::Ra)
        return {};
    if (backend_.mode == NpuBackend::Aiv) {
        aiv_launcher_ = std::make_unique<AivLauncher>();
        if (const auto loaded = aiv_launcher_->load(backend_.aiv_kernel); !loaded)
            return unexpected(loaded.error());
        return {};
    }
    aicpu_launcher_ = std::make_unique<AicpuLauncher>();
    if (const auto loaded = aicpu_launcher_->load(backend_.aicpu_kernel); !loaded)
        return unexpected(loaded.error());
    return {};
}

}  // namespace nds::client
