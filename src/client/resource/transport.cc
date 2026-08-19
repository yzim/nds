#include "transport.hh"

#include "aicpu/host/launcher.hh"
#include "aiv/host/launcher.hh"
#include "ra/ra.hh"

namespace nds::client {
namespace {
constexpr std::size_t kPrivateBufferBytes = 4096U;
}

Result<void> Transport::open(NpuRuntime *runtime, const TransportConfig &config, const ExecutionConfig &execution) {
    if (runtime == nullptr || runtime->context() == nullptr || runtime->memory() == nullptr || runtime_ != nullptr)
        return unexpected(ErrorCode::kInvalidArgument, "transport requires one open runtime");
    runtime_ = runtime;
    config_ = config;
    execution_ = execution;
    if (!qp_.create(&runtime_->context()->ra_api(), config_.qp, execution_.mode) || !qp_.make_qp_info(&local_))
        return unexpected(ErrorCode::kRa, qp_.error());
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
    if (!qp_.connect(*peer))
        return unexpected(ErrorCode::kRa, qp_.error());
    return ready();
}

Result<void> Transport::read(LocalAddress local, RemoteAddress remote, std::uint32_t length) {
    if (runtime_ == nullptr || execution_.mode != NpuExecutionMode::Ra || local.address == 0U || local.key == 0U ||
        remote.address == 0U || remote.key == 0U || length == 0U) {
        return unexpected(ErrorCode::kInvalidArgument, "invalid RA transport read");
    }
    const nds_device_transfer transfer{next_wr_id_++, {local.address, length, local.key}, remote.address, remote.key, 0U};
    return NdsRaRdmaRead({runtime_->context(), &qp_}, transfer);
}

Result<void> Transport::write(LocalAddress local, RemoteAddress remote, std::uint32_t length) {
    if (runtime_ == nullptr || execution_.mode != NpuExecutionMode::Ra || local.address == 0U || local.key == 0U ||
        remote.address == 0U || remote.key == 0U || length == 0U) {
        return unexpected(ErrorCode::kInvalidArgument, "invalid RA transport write");
    }
    const nds_device_transfer transfer{next_wr_id_++, {local.address, length, local.key}, remote.address, remote.key, 0U};
    return NdsRaRdmaWrite({runtime_->context(), &qp_}, transfer);
}

Result<void> Transport::send_bytes(const void *source, std::size_t size) {
    if (runtime_ == nullptr || source == nullptr || size == 0U || size > send_buffer_.size())
        return unexpected(ErrorCode::kInvalidArgument, "transport send requires a bounded source buffer");
    if (const auto copied = runtime_->memory()->copy_to_device(&send_buffer_, source, size); !copied)
        return unexpected(copied.error());
    const nds_device_transfer transfer{next_wr_id_++,
                                       {send_region_.local_address().address, static_cast<std::uint32_t>(size),
                                        send_region_.local_address().key},
                                       0U,
                                       0U,
                                       0U};
    if (execution_.mode == NpuExecutionMode::Ra)
        return NdsRaRdmaSend({runtime_->context(), &qp_}, transfer);

    const auto device_transport = qp_.make_device_transport();
    if (!device_transport)
        return unexpected(device_transport.error());
    DeviceBuffer result_buffer;
    if (const auto allocated = runtime_->memory()->allocate(sizeof(nds_device_operation_result), &result_buffer);
        !allocated)
        return unexpected(allocated.error());
    const nds_device_operation_result pending{NDS_DEVICE_OPERATION_INVALID_ARGUMENT, NDS_DEVICE_OPERATION_PATH_NONE,
                                              0, 0U};
    if (const auto copied = runtime_->memory()->copy_to_device(&result_buffer, &pending, sizeof(pending)); !copied)
        return unexpected(copied.error());

    nds_device_operation_request request{};
    request.transport = *device_transport;
    request.operation = NDS_DEVICE_RDMA_SEND;
    request.parameters.transfer = transfer;
    request.operation_result_address = reinterpret_cast<std::uint64_t>(result_buffer.data());
    std::string error;
    if (execution_.mode == NpuExecutionMode::Aicpu) {
        AicpuEntrypointLauncher launcher;
        if (!launcher.load(&runtime_->context()->acl_api(), execution_.aicpu_kernel_config) ||
            !launcher.launch_and_wait(&request, 5000))
            error = launcher.error();
    } else {
        AivEntrypointLauncher launcher;
        nds_device_operation_request device_request{};
        if (!launcher.load(&runtime_->context()->acl_api(), execution_.aiv_kernel) ||
            !launcher.make_device_request(request, &device_request)) {
            error = launcher.error();
        } else {
            DeviceBuffer request_buffer;
            if (const auto allocated = runtime_->memory()->allocate(sizeof(device_request), &request_buffer); !allocated ||
                !(runtime_->memory()->copy_to_device(&request_buffer, &device_request, sizeof(device_request))) ||
                !launcher.launch_and_wait(reinterpret_cast<std::uint64_t>(request_buffer.data()), request.operation,
                                          5000)) {
                error = launcher.error().empty() ? runtime_->context()->error() : launcher.error();
            }
        }
    }
    if (!error.empty())
        return unexpected(ErrorCode::kRuntime, error);
    nds_device_operation_result completed{};
    if (const auto copied = runtime_->memory()->copy_from_device(&completed, result_buffer, sizeof(completed)); !copied)
        return unexpected(copied.error());
    if (completed.status != NDS_DEVICE_OPERATION_SUCCESS)
        return unexpected(ErrorCode::kRuntime, "device transport send failed");
    return {};
}

TcpPeerExchange *Transport::bootstrap() noexcept { return &bootstrap_; }
const nds_qp_info &Transport::local_qp_info() const noexcept { return local_; }
NpuRuntime *Transport::runtime() noexcept { return runtime_; }
NpuRaQp *Transport::qp() noexcept { return &qp_; }
const ExecutionConfig &Transport::execution() const noexcept { return execution_; }

Result<void> Transport::ready() {
    int port = -1;
    int qp_status = -1;
    int lite = -1;
    if (!qp_.query_port_status(&port) || !qp_.query_status(&qp_status) || !qp_.query_support_lite(&lite) ||
        port != NDS_RA_PORT_STATUS_ACTIVE || qp_status != NDS_RA_QP_STATUS_CONNECTED ||
        lite == NDS_RA_LITE_NOT_SUPPORTED) {
        return unexpected(ErrorCode::kRa, qp_.error().empty() ? "client transport is not ready" : qp_.error());
    }
    return {};
}

Result<void> Transport::initialize_private_memory() {
    Memory *memory = runtime_->memory();
    if (const auto allocated = memory->allocate(kPrivateBufferBytes, &send_buffer_); !allocated)
        return unexpected(allocated.error());
    if (const auto allocated = memory->allocate(kPrivateBufferBytes, &receive_buffer_); !allocated)
        return unexpected(allocated.error());
    if (const auto registered = memory->register_memory(&qp_, &send_buffer_, &send_region_); !registered)
        return unexpected(registered.error());
    if (const auto registered = memory->register_memory(&qp_, &receive_buffer_, &receive_region_); !registered)
        return unexpected(registered.error());
    if (execution_.mode == NpuExecutionMode::Ra)
        return {};
    if (const auto allocated = memory->allocate(config_.qp.send_queue_depth * sizeof(std::uint64_t), &send_wr_ids_);
        !allocated)
        return unexpected(allocated.error());
    if (const auto allocated =
            memory->allocate(config_.qp.receive_queue_depth * sizeof(std::uint64_t), &receive_wr_ids_);
        !allocated)
        return unexpected(allocated.error());
    return qp_.set_device_wr_id_storage(reinterpret_cast<std::uint64_t>(send_wr_ids_.data()),
                                        reinterpret_cast<std::uint64_t>(receive_wr_ids_.data()));
}

}  // namespace nds::client
