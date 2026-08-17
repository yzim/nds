#include "storage.hh"

#include "aicpu/host/launcher.hh"
#include "aiv/host/launcher.hh"
#include "ra.hh"
#include "nds/protocol.h"

#include <chrono>
#include <cstddef>
#include <string>
#include <thread>

namespace nds::client {
namespace {

constexpr std::uint32_t kCompletionTimeoutMs = 5000U;

class DeviceAllocation {
public:
    explicit DeviceAllocation(NpuRaContext *context) : context_(context) {}
    ~DeviceAllocation() {
        if (context_ != nullptr && address_ != nullptr)
            (void)context_->free_device_memory(address_);
    }
    DeviceAllocation(const DeviceAllocation &) = delete;
    DeviceAllocation &operator=(const DeviceAllocation &) = delete;

    bool allocate(std::size_t size) {
        return context_ != nullptr && address_ == nullptr && context_->allocate_device_memory(size, &address_);
    }
    void *get() const noexcept { return address_; }

private:
    NpuRaContext *context_{};
    void *address_{};
};

Result<void> submit_device_storage_command(Connection *connection, const RegisteredRegion &command,
                                           std::uint32_t length) {
    if (connection == nullptr || connection->context() == nullptr || connection->qp() == nullptr ||
        !command.belongs_to(connection->qp()) || length == 0U || length > command.length()) {
        return unexpected(ErrorCode::kInvalidArgument, "storage command requires a registered command buffer");
    }
    const auto device_connection = connection->qp()->make_device_connection();
    if (!device_connection)
        return unexpected(device_connection.error());

    nds_device_operation_request request{};
    request.operation = NDS_DEVICE_RDMA_SEND;
    request.connection = *device_connection;
    request.parameters.transfer = {1U, {command.address(), length, command.local_key()}, 0U, 0U, 0U};

    DeviceAllocation result(connection->context());
    if (!result.allocate(sizeof(nds_device_operation_result)))
        return unexpected(ErrorCode::kRuntime, connection->context()->error());
    const nds_device_operation_result pending{NDS_DEVICE_OPERATION_INVALID_ARGUMENT, NDS_DEVICE_OPERATION_PATH_NONE,
                                              0, 0U};
    if (!connection->context()->copy_host_to_device(result.get(), &pending, sizeof(pending)))
        return unexpected(ErrorCode::kRuntime, connection->context()->error());
    request.operation_result_address = reinterpret_cast<std::uint64_t>(result.get());

    std::string launch_error;
    if (connection->config().execution == NpuExecutionMode::Aicpu) {
        AicpuConnectionLauncher launcher;
        if (!launcher.load(&connection->context()->acl_api(), connection->config().rma.aicpu_kernel_config) ||
            !launcher.launch_and_wait(&request, kCompletionTimeoutMs)) {
            launch_error = launcher.error();
        }
    } else {
        AivConnectionLauncher launcher;
        if (!launcher.load(&connection->context()->acl_api(), connection->config().rma.aiv_kernel) ||
            !launcher.make_device_request(request, &request)) {
            launch_error = launcher.error();
        } else {
            DeviceAllocation device_request(connection->context());
            if (!device_request.allocate(sizeof(request)) ||
                !connection->context()->copy_host_to_device(device_request.get(), &request, sizeof(request)) ||
                !launcher.launch_and_wait(reinterpret_cast<std::uint64_t>(device_request.get()), request.operation,
                                          kCompletionTimeoutMs)) {
                launch_error = launcher.error().empty() ? connection->context()->error() : launcher.error();
            }
        }
    }
    if (!launch_error.empty())
        return unexpected(ErrorCode::kRuntime, launch_error);

    nds_device_operation_result completed{};
    if (!connection->context()->copy_device_to_host(&completed, result.get(), sizeof(completed)))
        return unexpected(ErrorCode::kRuntime, connection->context()->error());
    if (completed.status != NDS_DEVICE_OPERATION_SUCCESS)
        return unexpected(ErrorCode::kRuntime, "device storage command submission failed");
    return {};
}

}  // namespace

Result<void> StorageClient::open(const ConnectionConfig &config) {
    if (opened_)
        return unexpected(ErrorCode::kInvalidArgument, "storage client is already open");
    if (const auto result = connection_.open(config); !result)
        return unexpected(result.error());
    if (const auto result = connection_.allocate(sizeof(nds_protocol_command_wire), &command_buffer_); !result)
        return unexpected(result.error());
    if (const auto result = connection_.allocate(sizeof(nds_protocol_completion_wire), &completion_buffer_); !result)
        return unexpected(result.error());
    if (const auto result = connection_.register_memory(&command_buffer_, &command_region_); !result)
        return unexpected(result.error());
    if (const auto result = connection_.register_memory(&completion_buffer_, &completion_region_); !result)
        return unexpected(result.error());
    const auto capacity = exchange_bootstrap();
    if (!capacity)
        return unexpected(capacity.error());
    capacity_ = *capacity;
    const nds_qp_info &local = connection_.local_qp_info();
    next_request_id_ = (static_cast<std::uint64_t>(local.qp_num) << 32U) | local.psn;
    if (next_request_id_ == 0U)
        next_request_id_ = 1U;
    opened_ = true;
    return {};
}

Result<void> StorageClient::allocate(std::size_t size, DeviceBuffer *buffer) {
    if (!opened_)
        return unexpected(ErrorCode::kInvalidArgument, "storage client is not open");
    return connection_.allocate(size, buffer);
}

Result<void> StorageClient::copy_to_device(DeviceBuffer *buffer, const void *source, std::size_t size) {
    if (!opened_)
        return unexpected(ErrorCode::kInvalidArgument, "storage client is not open");
    return connection_.copy_to_device(buffer, source, size);
}

Result<void> StorageClient::copy_from_device(void *destination, const DeviceBuffer &buffer, std::size_t size) {
    if (!opened_)
        return unexpected(ErrorCode::kInvalidArgument, "storage client is not open");
    return connection_.copy_from_device(destination, buffer, size);
}

Result<void> StorageClient::read(std::uint64_t offset, DeviceBuffer *data, std::uint32_t length) {
    return execute(NDS_PROTOCOL_READ, offset, data, length);
}

Result<void> StorageClient::write(std::uint64_t offset, DeviceBuffer *data, std::uint32_t length) {
    return execute(NDS_PROTOCOL_WRITE, offset, data, length);
}

std::uint64_t StorageClient::capacity() const noexcept {
    return capacity_;
}

Result<void> StorageClient::execute(std::uint16_t operation, std::uint64_t offset, DeviceBuffer *data,
                                    std::uint32_t length) {
    if (!opened_ || data == nullptr || length == 0U || length > data->size())
        return unexpected(ErrorCode::kInvalidArgument, "storage operation requires an open client and data buffer");
    if (request_submitted_)
        return unexpected(ErrorCode::kUnsupported, "the initial storage client supports one command per connection");
    if (offset > capacity_ || length > capacity_ - offset)
        return unexpected(ErrorCode::kProtocol, "requested storage range exceeds namespace capacity");

    RegisteredRegion data_region;
    if (const auto result = connection_.register_memory(data, &data_region); !result)
        return unexpected(result.error());
    const auto data_remote = connection_.remote_region(data_region);
    if (!data_remote)
        return unexpected(data_remote.error());

    const std::uint64_t request_id = next_request_id_++;
    const std::uint32_t remote_access =
        operation == NDS_PROTOCOL_READ ? NDS_PROTOCOL_ACCESS_REMOTE_WRITE : NDS_PROTOCOL_ACCESS_REMOTE_READ;
    if (const auto result = connection_.ready(); !result)
        return unexpected(result.error());
    if (connection_.config().execution == NpuExecutionMode::Ra) {
        request_submitted_ = true;
        return execute_ra_storage({connection_.context(), connection_.qp(), command_buffer_.data(),
                                        {command_region_.address(), static_cast<std::uint32_t>(command_region_.length()), command_region_.local_key()},
                                        completion_buffer_.data(),
                                        {completion_region_.address(), static_cast<std::uint32_t>(completion_region_.length()), completion_region_.local_key()},
                                        {data_remote->address, data_remote->length, data_remote->key, remote_access}, operation,
                                        offset, length, capacity_, request_id});
    }
    nds_protocol_completion_wire completion_wire{};
    const nds_protocol_completion pending{request_id, NDS_PROTOCOL_COMPLETION_PENDING, NDS_PROTOCOL_SUCCESS, 0U};
    if (nds_protocol_completion_encode(&pending, &completion_wire) != NDS_PROTOCOL_RESULT_OK)
        return unexpected(ErrorCode::kProtocol, "invalid storage completion record");
    if (const auto result =
            connection_.copy_to_device(&completion_buffer_, &completion_wire, sizeof(completion_wire));
        !result) {
        return unexpected(result.error());
    }

    const nds_protocol_memory remote_data{data_remote->address, data_remote->length, data_remote->key, remote_access};
    const nds_protocol_command command{request_id, operation, offset, length, remote_data};
    nds_protocol_command_wire command_wire{};
    if (nds_protocol_command_encode(&command, &command_wire) != NDS_PROTOCOL_RESULT_OK)
        return unexpected(ErrorCode::kProtocol, "invalid storage command record");
    if (const auto result = connection_.copy_to_device(&command_buffer_, &command_wire, sizeof(command_wire)); !result)
        return unexpected(result.error());
    request_submitted_ = true;
    if (const auto result = submit_device_storage_command(&connection_, command_region_, sizeof(command_wire)); !result)
        return unexpected(result.error());
    return wait_for_completion(request_id, length);
}

Result<std::uint64_t> StorageClient::exchange_bootstrap() {
    const auto completion = connection_.remote_region(completion_region_);
    if (!completion)
        return unexpected(completion.error());
    const nds_protocol_bootstrap bootstrap{
        {completion->address, completion->length, completion->key, NDS_PROTOCOL_ACCESS_REMOTE_WRITE}};
    nds_protocol_bootstrap_wire bootstrap_wire{};
    nds_protocol_namespace_wire namespace_wire{};
    nds_protocol_namespace namespace_record{};
    if (nds_protocol_bootstrap_encode(&bootstrap, &bootstrap_wire) != NDS_PROTOCOL_RESULT_OK)
        return unexpected(ErrorCode::kProtocol, "invalid storage bootstrap record");
    if (const auto sent = connection_.bootstrap()->send_bytes(&bootstrap_wire, sizeof(bootstrap_wire)); !sent)
        return unexpected(sent.error());
    if (const auto received = connection_.bootstrap()->receive_bytes(&namespace_wire, sizeof(namespace_wire)); !received)
        return unexpected(received.error());
    if (nds_protocol_namespace_decode(&namespace_wire, &namespace_record) != NDS_PROTOCOL_RESULT_OK)
        return unexpected(ErrorCode::kProtocol, "invalid storage namespace record");
    return namespace_record.capacity;
}

Result<void> StorageClient::wait_for_completion(std::uint64_t request_id, std::uint64_t expected_bytes) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(kCompletionTimeoutMs);
    while (std::chrono::steady_clock::now() < deadline) {
        nds_protocol_completion_wire wire{};
        nds_protocol_completion completion{};
        if (const auto result = connection_.copy_from_device(&wire, completion_buffer_, sizeof(wire)); !result)
            return unexpected(result.error());
        if (nds_protocol_completion_decode(&wire, &completion) != NDS_PROTOCOL_RESULT_OK)
            return unexpected(ErrorCode::kProtocol, "invalid storage completion record");
        if (completion.state == NDS_PROTOCOL_COMPLETION_COMPLETE) {
            if (completion.request_id != request_id || completion.status != NDS_PROTOCOL_SUCCESS ||
                completion.bytes_transferred != expected_bytes) {
                return unexpected(ErrorCode::kProtocol, "storage completion does not match the request");
            }
            return {};
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return unexpected(ErrorCode::kProtocol, "timed out waiting for storage completion");
}

}  // namespace nds::client
