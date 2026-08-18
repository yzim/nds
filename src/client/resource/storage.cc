#include "storage.hh"

#include "aicpu/host/launcher.hh"
#include "aiv/host/launcher.hh"
#include "ra/ra.hh"
#include "nds/protocol.h"

#include <cstddef>
#include <string>

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

Result<void> submit_device_storage(Connection *connection, const RegisteredRegion &command,
                                   const RegisteredRegion &completion, const RegisteredRegion &data,
                                   std::uint16_t operation, std::uint64_t offset, std::uint32_t length,
                                   std::uint64_t capacity, std::uint64_t request_id) {
    if (connection == nullptr || connection->context() == nullptr || connection->qp() == nullptr ||
        !command.belongs_to(connection->qp()) || !completion.belongs_to(connection->qp()) ||
        !data.belongs_to(connection->qp()) || data.address() == 0U || data.local_key() == 0U ||
        data.remote_key() == 0U || length == 0U || length > data.length() || request_id == 0U) {
        return unexpected(ErrorCode::kInvalidArgument, "storage submission requires registered storage buffers");
    }
    const auto device_connection = connection->qp()->make_device_connection();
    if (!device_connection)
        return unexpected(device_connection.error());

    nds_device_storage_request request{};
    request.storage.abi_version = NDS_DEVICE_STORAGE_ABI_VERSION;
    request.storage.size = sizeof(request.storage);
    request.storage.connection = *device_connection;
    request.storage.command = {command.address(), static_cast<std::uint32_t>(command.length()), command.local_key()};
    request.storage.completion =
        {completion.address(), static_cast<std::uint32_t>(completion.length()), completion.local_key()};
    request.storage.capacity = capacity;
    request.storage.request_id = request_id;
    request.io.operation = operation;
    request.io.length = length;
    request.io.offset = offset;
    request.io.data = {data.address(), static_cast<std::uint32_t>(data.length()), data.local_key()};
    request.io.data_rkey = data.remote_key();

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
        AicpuEntrypointLauncher launcher;
        if (!launcher.load(&connection->context()->acl_api(), connection->config().rma.aicpu_kernel_config) ||
            !launcher.launch_storage_and_wait(&request, kCompletionTimeoutMs)) {
            launch_error = launcher.error();
        }
    } else {
        AivEntrypointLauncher launcher;
        request.abi_version = NDS_DEVICE_STORAGE_ABI_VERSION;
        request.size = sizeof(request);
        if (!launcher.load(&connection->context()->acl_api(), connection->config().rma.aiv_kernel)) {
            launch_error = launcher.error();
        } else {
            DeviceAllocation device_request(connection->context());
            if (!device_request.allocate(sizeof(request)) ||
                !connection->context()->copy_host_to_device(device_request.get(), &request, sizeof(request)) ||
                !launcher.launch_storage_and_wait(reinterpret_cast<std::uint64_t>(device_request.get()), operation,
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
        return unexpected(ErrorCode::kRuntime, "device storage operation failed");
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
    const std::uint64_t request_id = next_request_id_++;
    const std::uint32_t remote_access =
        operation == NDS_PROTOCOL_READ ? NDS_PROTOCOL_ACCESS_REMOTE_WRITE : NDS_PROTOCOL_ACCESS_REMOTE_READ;
    if (const auto result = connection_.ready(); !result)
        return unexpected(result.error());
    if (connection_.config().execution == NpuExecutionMode::Ra) {
        request_submitted_ = true;
        const RaStorageRequest request{{connection_.context(), connection_.qp()},
                                       command_buffer_.data(),
                                       {command_region_.address(), static_cast<std::uint32_t>(command_region_.length()),
                                        command_region_.local_key()},
                                       completion_buffer_.data(),
                                       {completion_region_.address(),
                                        static_cast<std::uint32_t>(completion_region_.length()),
                                        completion_region_.local_key()},
                                       {data_region.address(), data_region.length(), data_region.remote_key(),
                                        remote_access},
                                       offset,
                                       length,
                                       capacity_,
                                       request_id};
        return operation == NDS_PROTOCOL_READ ? NdsRaStorageRead(request) : NdsRaStorageWrite(request);
    }
    request_submitted_ = true;
    if (const auto result = submit_device_storage(&connection_, command_region_, completion_region_, data_region,
                                                  operation, offset, length, capacity_, request_id);
        !result)
        return unexpected(result.error());
    return {};
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

}  // namespace nds::client
