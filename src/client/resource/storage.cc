#include "storage.hh"

#include "aicpu/host/launcher.hh"
#include "aiv/host/launcher.hh"
#include "ra/ra.hh"
#include "nds/protocol.h"

#include <cstddef>
#include <string>
#include <utility>

namespace nds::client {
namespace {

constexpr std::uint32_t kCompletionTimeoutMs = 5000U;

class DeviceAllocation {
public:
    explicit DeviceAllocation(Runtime *runtime) : runtime_(runtime) {}
    ~DeviceAllocation() = default;
    DeviceAllocation(const DeviceAllocation &) = delete;
    DeviceAllocation &operator=(const DeviceAllocation &) = delete;

    Result<void> allocate(std::size_t size) {
        if (runtime_ == nullptr || buffer_.data() != nullptr)
            return unexpected(ErrorCode::kInvalidArgument, "device allocation requires one runtime and empty storage");
        auto allocated = runtime_->allocate(size);
        if (!allocated)
            return unexpected(allocated.error());
        buffer_ = std::move(*allocated);
        return {};
    }
    void *get() const noexcept {
        return buffer_.data();
    }

private:
    Runtime *runtime_{};
    MemoryBuffer buffer_;
};

Result<void> submit_device_storage(Runtime *runtime, Transport *transport, const MemoryRegion &command,
                                   const MemoryRegion &completion, const MemoryRegion &data, std::uint16_t operation,
                                   std::uint64_t offset, std::uint32_t length, std::uint64_t capacity,
                                   std::uint64_t request_id) {
    if (runtime == nullptr || transport == nullptr || !runtime->initialized() || transport->qp() == nullptr ||
        !command.belongs_to(transport->endpoint()) || !completion.belongs_to(transport->endpoint()) ||
        !data.belongs_to(transport->endpoint()) || data.address() == 0U || data.local_key() == 0U ||
        data.remote_key() == 0U || length == 0U || length > data.length() || request_id == 0U) {
        return unexpected(ErrorCode::kInvalidArgument, "storage submission requires registered storage buffers");
    }
    const auto device_transport = transport->qp()->make_device_transport();
    if (!device_transport)
        return unexpected(device_transport.error());

    nds_device_storage_request request{};
    request.storage.abi_version = NDS_DEVICE_STORAGE_ABI_VERSION;
    request.storage.size = sizeof(request.storage);
    request.storage.transport = *device_transport;
    request.storage.command = {command.address(), static_cast<std::uint32_t>(command.length()), command.local_key()};
    request.storage.completion = {completion.address(), static_cast<std::uint32_t>(completion.length()),
                                  completion.local_key()};
    request.storage.capacity = capacity;
    request.storage.request_id = request_id;
    request.io.operation = operation;
    request.io.length = length;
    request.io.offset = offset;
    request.io.data = {data.address(), static_cast<std::uint32_t>(data.length()), data.local_key()};
    request.io.data_rkey = data.remote_key();

    DeviceAllocation result(runtime);
    if (const auto allocated = result.allocate(sizeof(nds_device_operation_result)); !allocated)
        return unexpected(allocated.error());
    const nds_device_operation_result pending{NDS_DEVICE_OPERATION_INVALID_ARGUMENT, NDS_DEVICE_OPERATION_PATH_NONE, 0,
                                              0U};
    if (const auto copied = runtime->copy_host_to_device(result.get(), &pending, sizeof(pending)); !copied)
        return unexpected(copied.error());
    request.operation_result_address = reinterpret_cast<std::uint64_t>(result.get());

    if (transport->execution().mode == NpuExecutionMode::Aicpu) {
        AicpuEntrypointLauncher launcher;
        if (const auto loaded = launcher.load(&runtime->acl_api(), transport->execution().aicpu_kernel_config); !loaded)
            return unexpected(loaded.error());
        if (const auto launched = launcher.launch_storage_and_wait(&request, kCompletionTimeoutMs); !launched)
            return unexpected(launched.error());
    } else {
        AivEntrypointLauncher launcher;
        request.abi_version = NDS_DEVICE_STORAGE_ABI_VERSION;
        request.size = sizeof(request);
        if (const auto loaded = launcher.load(&runtime->acl_api(), transport->execution().aiv_kernel); !loaded)
            return unexpected(loaded.error());
        DeviceAllocation device_request(runtime);
        if (const auto allocated = device_request.allocate(sizeof(request)); !allocated)
            return unexpected(allocated.error());
        if (const auto copied = runtime->copy_host_to_device(device_request.get(), &request, sizeof(request)); !copied)
            return unexpected(copied.error());
        if (const auto launched = launcher.launch_storage_and_wait(
                reinterpret_cast<std::uint64_t>(device_request.get()), operation, kCompletionTimeoutMs);
            !launched)
            return unexpected(launched.error());
    }

    nds_device_operation_result completed{};
    if (const auto copied = runtime->copy_device_to_host(&completed, result.get(), sizeof(completed)); !copied)
        return unexpected(copied.error());
    if (completed.status != NDS_DEVICE_OPERATION_SUCCESS)
        return unexpected(ErrorCode::kRuntime, "device storage operation failed");
    return {};
}

}  // namespace

Result<void> StorageClient::open(Runtime *runtime, Transport *transport) {
    if (opened_ || runtime == nullptr || transport == nullptr || !runtime->initialized())
        return unexpected(ErrorCode::kInvalidArgument, "storage client requires one open runtime and transport");
    runtime_ = runtime;
    transport_ = transport;
    auto command_buffer = runtime_->allocate(sizeof(nds_protocol_command_wire));
    if (!command_buffer)
        return unexpected(command_buffer.error());
    command_buffer_ = std::move(*command_buffer);
    auto completion_buffer = runtime_->allocate(sizeof(nds_protocol_completion_wire));
    if (!completion_buffer)
        return unexpected(completion_buffer.error());
    completion_buffer_ = std::move(*completion_buffer);
    auto command_region = transport_->endpoint()->reg_mr(command_buffer_, MemoryAccess::DirectNpu);
    if (!command_region)
        return unexpected(command_region.error());
    command_region_ = std::move(*command_region);
    auto completion_region = transport_->endpoint()->reg_mr(completion_buffer_, MemoryAccess::DirectNpu);
    if (!completion_region)
        return unexpected(completion_region.error());
    completion_region_ = std::move(*completion_region);
    const auto capacity = exchange_bootstrap();
    if (!capacity)
        return unexpected(capacity.error());
    capacity_ = *capacity;
    const nds_qp_info &local = transport_->local_qp_info();
    next_request_id_ = (static_cast<std::uint64_t>(local.qp_num) << 32U) | local.psn;
    if (next_request_id_ == 0U)
        next_request_id_ = 1U;
    opened_ = true;
    return {};
}

Result<void> StorageClient::read(std::uint64_t offset, MemoryBuffer *data, std::uint32_t length) {
    return execute(NDS_PROTOCOL_READ, offset, data, length);
}

Result<void> StorageClient::write(std::uint64_t offset, MemoryBuffer *data, std::uint32_t length) {
    return execute(NDS_PROTOCOL_WRITE, offset, data, length);
}

std::uint64_t StorageClient::capacity() const noexcept {
    return capacity_;
}

Result<void> StorageClient::execute(std::uint16_t operation, std::uint64_t offset, MemoryBuffer *data,
                                    std::uint32_t length) {
    if (!opened_ || data == nullptr || length == 0U || length > data->size())
        return unexpected(ErrorCode::kInvalidArgument, "storage operation requires an open client and data buffer");
    if (offset > capacity_ || length > capacity_ - offset)
        return unexpected(ErrorCode::kProtocol, "requested storage range exceeds namespace capacity");

    auto registered = transport_->endpoint()->reg_mr(*data, MemoryAccess::DirectNpu);
    if (!registered)
        return unexpected(registered.error());
    MemoryRegion data_region = std::move(*registered);
    const std::uint64_t request_id = next_request_id_++;
    const std::uint32_t remote_access =
        operation == NDS_PROTOCOL_READ ? NDS_PROTOCOL_ACCESS_REMOTE_WRITE : NDS_PROTOCOL_ACCESS_REMOTE_READ;
    if (const auto result = transport_->ready(); !result)
        return unexpected(result.error());
    if (transport_->execution().mode == NpuExecutionMode::Ra) {
        const RaStorageRequest request{
            {runtime_, transport_->qp()},
            command_buffer_.data(),
            {command_region_.address(), static_cast<std::uint32_t>(command_region_.length()),
             command_region_.local_key()},
            completion_buffer_.data(),
            {completion_region_.address(), static_cast<std::uint32_t>(completion_region_.length()),
             completion_region_.local_key()},
            {data_region.address(), data_region.length(), data_region.remote_key(), remote_access},
            offset,
            length,
            capacity_,
            request_id};
        return operation == NDS_PROTOCOL_READ ? NdsRaStorageRead(request) : NdsRaStorageWrite(request);
    }
    if (const auto result = submit_device_storage(runtime_, transport_, command_region_, completion_region_,
                                                  data_region, operation, offset, length, capacity_, request_id);
        !result)
        return unexpected(result.error());
    return {};
}

Result<std::uint64_t> StorageClient::exchange_bootstrap() {
    const nds_protocol_bootstrap bootstrap{{completion_region_.address(), completion_region_.length(),
                                            completion_region_.remote_key(), NDS_PROTOCOL_ACCESS_REMOTE_WRITE}};
    nds_protocol_bootstrap_wire bootstrap_wire{};
    nds_protocol_namespace_wire namespace_wire{};
    nds_protocol_namespace namespace_record{};
    if (nds_protocol_bootstrap_encode(&bootstrap, &bootstrap_wire) != NDS_PROTOCOL_RESULT_OK)
        return unexpected(ErrorCode::kProtocol, "invalid storage bootstrap record");
    if (const auto sent = transport_->bootstrap()->send_bytes(&bootstrap_wire, sizeof(bootstrap_wire)); !sent)
        return unexpected(sent.error());
    if (const auto received = transport_->bootstrap()->receive_bytes(&namespace_wire, sizeof(namespace_wire));
        !received)
        return unexpected(received.error());
    if (nds_protocol_namespace_decode(&namespace_wire, &namespace_record) != NDS_PROTOCOL_RESULT_OK)
        return unexpected(ErrorCode::kProtocol, "invalid storage namespace record");
    return namespace_record.capacity;
}

}  // namespace nds::client
