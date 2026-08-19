#include "ra.hh"

#include <chrono>
#include <thread>

namespace nds {
namespace {
constexpr std::uint32_t kCompletionTimeoutMs = 5000U;

Result<void> validate(const RaStorageRequest &request) {
    if (request.connection.context == nullptr || request.connection.qp == nullptr ||
        request.connection.qp->execution_mode() != NpuExecutionMode::Ra || request.command_device == nullptr ||
        request.completion_device == nullptr || request.command.address == 0U || request.command.local_key == 0U ||
        request.command.length < sizeof(nds_protocol_command_wire) || request.completion.address == 0U ||
        request.completion.local_key == 0U || request.completion.length < sizeof(nds_protocol_completion_wire) ||
        request.request_id == 0U || request.length == 0U || request.offset > request.capacity ||
        request.length > request.capacity - request.offset || request.remote_data.address == 0U ||
        request.remote_data.rkey == 0U || request.remote_data.length < request.length) {
        return unexpected(ErrorCode::kInvalidArgument, "invalid RA storage request");
    }
    return {};
}

Result<void> execute(const RaStorageRequest &request, std::uint16_t operation) {
    if (const auto valid = validate(request); !valid)
        return unexpected(valid.error());

    const nds_protocol_completion pending{request.request_id, NDS_PROTOCOL_COMPLETION_PENDING, NDS_PROTOCOL_SUCCESS,
                                          0U};
    nds_protocol_completion_wire pending_wire{};
    if (nds_protocol_completion_encode(&pending, &pending_wire) != NDS_PROTOCOL_RESULT_OK)
        return unexpected(ErrorCode::kProtocol, "invalid RA pending completion record");
    if (!request.connection.context->copy_host_to_device(request.completion_device, &pending_wire,
                                                         sizeof(pending_wire)))
        return unexpected(ErrorCode::kRuntime, request.connection.context->error());

    const nds_protocol_command command{request.request_id, operation, request.offset, request.length,
                                       request.remote_data};
    nds_protocol_command_wire command_wire{};
    if (nds_protocol_command_encode(&command, &command_wire) != NDS_PROTOCOL_RESULT_OK)
        return unexpected(ErrorCode::kProtocol, "invalid RA storage command record");
    if (!request.connection.context->copy_host_to_device(request.command_device, &command_wire, sizeof(command_wire)))
        return unexpected(ErrorCode::kRuntime, request.connection.context->error());

    const nds_device_transfer transfer{
        request.request_id, {request.command.address, request.command.length, request.command.local_key}, 0U, 0U, 0U};
    if (const auto posted = NdsRaRdmaSend(request.connection, transfer); !posted)
        return unexpected(posted.error());

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(kCompletionTimeoutMs);
    while (std::chrono::steady_clock::now() < deadline) {
        nds_protocol_completion_wire wire{};
        nds_protocol_completion completion{};
        if (!request.connection.context->copy_device_to_host(&wire, request.completion_device, sizeof(wire)))
            return unexpected(ErrorCode::kRuntime, request.connection.context->error());
        if (nds_protocol_completion_decode(&wire, &completion) != NDS_PROTOCOL_RESULT_OK)
            return unexpected(ErrorCode::kProtocol, "invalid RA storage completion record");
        if (completion.state == NDS_PROTOCOL_COMPLETION_COMPLETE) {
            if (completion.request_id != request.request_id || completion.status != NDS_PROTOCOL_SUCCESS ||
                completion.bytes_transferred != request.length)
                return unexpected(ErrorCode::kProtocol, "RA storage completion does not match request");
            return {};
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return unexpected(ErrorCode::kProtocol, "timed out waiting for RA storage completion");
}
}  // namespace

Result<void> NdsRaStorageRead(const RaStorageRequest &request) {
    return execute(request, NDS_PROTOCOL_READ);
}

Result<void> NdsRaStorageWrite(const RaStorageRequest &request) {
    return execute(request, NDS_PROTOCOL_WRITE);
}

}  // namespace nds
