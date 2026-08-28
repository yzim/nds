#include "transport.hh"

#include "aicpu/host/launcher.hh"
#include "aiv/host/launcher.hh"
#include "ra/ra.hh"

#include <cstddef>
#include <limits>
#include <string>
#include <utility>

namespace nds::client {
namespace {

constexpr std::uint32_t kTransportLaunchTimeoutMs = 5000U;

enum class SendOperation { Send, Read, Write };

struct AivThreeAddressArguments {
    std::uint64_t first_address;
    std::uint64_t second_address;
    std::uint64_t return_value_address;
};

Result<void> launch_send(Runtime *runtime, const BackendConfig &backend, const NdsDeviceTransport &device_transport,
                         const NdsDeviceSendWr &wr, SendOperation operation) {
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
        AicpuLauncher launcher;
        if (const auto loaded = launcher.load(backend.aicpu_kernel_config); !loaded)
            return unexpected(loaded.error());
        if (const auto launched = launcher.launch_and_wait(
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
    AivLauncher launcher;
    if (const auto loaded = launcher.load(backend.aiv_kernel); !loaded)
        return unexpected(loaded.error());
    const std::uint64_t request_address = reinterpret_cast<std::uint64_t>(request_buffer->data());
    AivThreeAddressArguments arguments{request_address + offsetof(NdsDeviceRdmaSendArgs, transport),
                                       request_address + offsetof(NdsDeviceRdmaSendArgs, wr),
                                       request_address + offsetof(NdsDeviceRdmaSendArgs, return_value)};
    if (const auto launched =
            launcher.launch_and_wait(aiv_kernel, &arguments, sizeof(arguments), kTransportLaunchTimeoutMs);
        !launched) {
        return unexpected(launched.error());
    }
    if (const auto copied = runtime->copy_from(&request, *request_buffer, sizeof(request)); !copied)
        return unexpected(copied.error());
    if (request.return_value != 0)
        return unexpected(ErrorCode::kRuntime, "device transport send failed: " + std::to_string(request.return_value));
    return {};
}

Result<void> launch_receive(Runtime *runtime, const BackendConfig &backend, const NdsDeviceTransport &device_transport,
                            const NdsDeviceRecvWr &wr) {
    if (backend.mode == NpuBackend::Aicpu) {
        NdsDeviceRdmaRecvArgs request{device_transport, wr, std::numeric_limits<std::int32_t>::min()};
        auto request_buffer = runtime->allocate(sizeof(request));
        if (!request_buffer)
            return unexpected(request_buffer.error());
        if (const auto copied = runtime->copy_to(&*request_buffer, &request, sizeof(request)); !copied)
            return unexpected(copied.error());
        AicpuLauncher launcher;
        if (const auto loaded = launcher.load(backend.aicpu_kernel_config); !loaded)
            return unexpected(loaded.error());
        if (const auto launched = launcher.launch_and_wait("nds_aicpu_rdma_recv_kernel",
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
    AivLauncher launcher;
    if (const auto loaded = launcher.load(backend.aiv_kernel); !loaded)
        return unexpected(loaded.error());
    const std::uint64_t request_address = reinterpret_cast<std::uint64_t>(request_buffer->data());
    AivThreeAddressArguments arguments{request_address + offsetof(NdsDeviceRdmaRecvArgs, transport),
                                       request_address + offsetof(NdsDeviceRdmaRecvArgs, wr),
                                       request_address + offsetof(NdsDeviceRdmaRecvArgs, return_value)};
    if (const auto launched = launcher.launch_and_wait("nds_aiv_rdma_recv_kernel", &arguments, sizeof(arguments),
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

}  // namespace

bool QueueHandle::valid() const noexcept {
    return owner_ != nullptr && index_ != static_cast<std::size_t>(-1);
}

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
    const auto server = parse_tcp_address(config_.server_address);
    if (!server)
        return unexpected(server.error());
    auto connected = TcpPeerExchange::connect(server->ipv4, server->port, config_.tcp_timeout_ms);
    if (!connected)
        return unexpected(connected.error());
    bootstrap_ = std::move(*connected);
    const auto accepted_qp_count = bootstrap_.negotiate_qp_count_as_client(config_.qp_count);
    if (!accepted_qp_count)
        return unexpected(accepted_qp_count.error());

    qps_.reserve(*accepted_qp_count);
    local_qps_.reserve(*accepted_qp_count);
    next_wr_ids_.reserve(*accepted_qp_count);
    for (std::uint32_t index = 0U; index < *accepted_qp_count; ++index) {
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
    return submit_send(queue, request.local, request.length, NDS_DEVICE_WR_SEND, nullptr);
}

Result<void> Transport::receive(QueueHandle queue, const TransportReceive &request) {
    return submit_receive(queue, request);
}

Result<void> Transport::read(QueueHandle queue, const TransportRead &request) {
    return submit_send(queue, request.local, request.length, NDS_DEVICE_WR_RDMA_READ, &request.remote);
}

Result<void> Transport::write(QueueHandle queue, const TransportWrite &request) {
    return submit_send(queue, request.local, request.length, NDS_DEVICE_WR_RDMA_WRITE, &request.remote);
}

Result<void> Transport::send_batch(QueueHandle queue, std::span<const TransportSend> requests) {
    if (requests.empty())
        return unexpected(ErrorCode::kInvalidArgument, "transport send batch requires at least one request");
    if (requests.size() != 1U)
        return unexpected(ErrorCode::kUnsupported,
                          "multi-request transport batches require the CQ-credit implementation");
    return send(queue, requests.front());
}

Result<void> Transport::receive_batch(QueueHandle queue, std::span<const TransportReceive> requests) {
    if (requests.empty())
        return unexpected(ErrorCode::kInvalidArgument, "transport receive batch requires at least one request");
    if (requests.size() != 1U)
        return unexpected(ErrorCode::kUnsupported,
                          "multi-request transport batches require the CQ-credit implementation");
    return receive(queue, requests.front());
}

Result<void> Transport::read_batch(QueueHandle queue, std::span<const TransportRead> requests) {
    if (requests.empty())
        return unexpected(ErrorCode::kInvalidArgument, "transport read batch requires at least one request");
    if (requests.size() != 1U)
        return unexpected(ErrorCode::kUnsupported,
                          "multi-request transport batches require the CQ-credit implementation");
    return read(queue, requests.front());
}

Result<void> Transport::write_batch(QueueHandle queue, std::span<const TransportWrite> requests) {
    if (requests.empty())
        return unexpected(ErrorCode::kInvalidArgument, "transport write batch requires at least one request");
    if (requests.size() != 1U)
        return unexpected(ErrorCode::kUnsupported,
                          "multi-request transport batches require the CQ-credit implementation");
    return write(queue, requests.front());
}

Result<void> Transport::submit_send(QueueHandle queue, const MemoryRegion *local, std::uint32_t length,
                                    std::uint32_t opcode, const RemoteMemory *remote) {
    QueuePair *const qp = queue_pair(queue);
    if (runtime_ == nullptr || qp == nullptr || local == nullptr || !local->belongs_to(&endpoint_) || length == 0U ||
        length > local->length()) {
        return unexpected(ErrorCode::kInvalidArgument,
                          "transport send requires a queue and a valid local memory range");
    }
    if ((remote == nullptr) != (opcode == NDS_DEVICE_WR_SEND))
        return unexpected(ErrorCode::kInvalidArgument, "transport send operation has invalid remote-memory metadata");
    if (remote != nullptr && (remote->address == 0U || remote->key == 0U || remote->length < length)) {
        return unexpected(ErrorCode::kInvalidArgument, "RDMA operation exceeds its valid remote-memory range");
    }
    const std::size_t index = queue.index_;
    NdsDeviceSendWr request{next_wr_ids_[index]++,
                            opcode,
                            0U,
                            {local->address(), length, local->local_key()},
                            remote == nullptr ? 0U : remote->address,
                            remote == nullptr ? 0U : remote->key,
                            0U};
    if (next_wr_ids_[index] == 0U)
        ++next_wr_ids_[index];
    if (backend_.mode == NpuBackend::Ra)
        return NdsRaPostSend(runtime_, qp, request);
    const auto device_transport = qp->make_device_transport();
    if (!device_transport)
        return unexpected(device_transport.error());
    const SendOperation operation = opcode == NDS_DEVICE_WR_SEND        ? SendOperation::Send
                                    : opcode == NDS_DEVICE_WR_RDMA_READ ? SendOperation::Read
                                                                        : SendOperation::Write;
    return launch_send(runtime_, backend_, *device_transport, request, operation);
}

Result<void> Transport::submit_receive(QueueHandle queue, const TransportReceive &request) {
    QueuePair *const qp = queue_pair(queue);
    if (qp == nullptr || request.local == nullptr || !request.local->belongs_to(&endpoint_) || request.length == 0U ||
        request.length > request.local->length()) {
        return unexpected(ErrorCode::kInvalidArgument,
                          "transport receive requires a queue and a valid local memory range");
    }
    const std::size_t index = queue.index_;
    NdsDeviceRecvWr recv{next_wr_ids_[index]++, {request.local->address(), request.length, request.local->local_key()}};
    if (next_wr_ids_[index] == 0U)
        ++next_wr_ids_[index];
    if (backend_.mode == NpuBackend::Ra)
        return NdsRaPostRecv(qp, recv);
    const auto device_transport = qp->make_device_transport();
    if (!device_transport)
        return unexpected(device_transport.error());
    return launch_receive(runtime_, backend_, *device_transport, recv);
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
