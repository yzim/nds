#include "protocol.hh"

#include "nds/protocol.h"

#include <chrono>
#include <thread>

namespace nds::client {
namespace {

constexpr std::uint32_t kCompletionTimeoutMs = 5000U;

Result<std::uint64_t> exchange_bootstrap(Connection *connection, const RemoteRegion &completion) {
    const nds_protocol_bootstrap bootstrap{
        {completion.address, completion.length, completion.key, NDS_PROTOCOL_ACCESS_REMOTE_WRITE}};
    nds_protocol_bootstrap_wire bootstrap_wire{};
    nds_protocol_namespace_wire namespace_wire{};
    nds_protocol_namespace namespace_record{};
    if (nds_protocol_bootstrap_encode(&bootstrap, &bootstrap_wire) != 0) {
        return failure(ErrorCode::kProtocol, "invalid protocol record");
    }
    if (const auto sent = connection->bootstrap()->send_bytes(&bootstrap_wire, sizeof(bootstrap_wire)); !sent)
        return propagate(sent.error());
    if (const auto received = connection->bootstrap()->receive_bytes(&namespace_wire, sizeof(namespace_wire));
        !received)
        return propagate(received.error());
    if (nds_protocol_namespace_decode(&namespace_wire, &namespace_record) != 0) {
        return failure(ErrorCode::kProtocol, "invalid protocol record");
    }
    return namespace_record.capacity;
}

Result<void> wait_for_completion(Connection *connection, const DeviceBuffer &buffer, std::uint64_t request_id,
                                 std::uint64_t expected_bytes) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(kCompletionTimeoutMs);
    while (std::chrono::steady_clock::now() < deadline) {
        nds_protocol_completion_wire wire{};
        nds_protocol_completion completion{};
        if (const auto result = connection->copy_from_device(&wire, buffer, sizeof(wire)); !result) {
            return propagate(result.error());
        }
        if (nds_protocol_completion_decode(&wire, &completion) != 0) {
            return failure(ErrorCode::kProtocol, "invalid protocol record");
        }
        if (completion.state == NDS_PROTOCOL_COMPLETION_COMPLETE) {
            if (completion.request_id != request_id || completion.status != NDS_PROTOCOL_SUCCESS ||
                completion.bytes_transferred != expected_bytes) {
                return failure(ErrorCode::kProtocol, "storage completion does not match the request");
            }
            return success();
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return failure(ErrorCode::kProtocol, "timed out waiting for server completion");
}

}  // namespace

Result<void> execute_request(Connection *connection, const Request &request) {
    if (connection == nullptr || request.data == nullptr || request.length == 0U ||
        request.length > request.data->size())
        return failure(ErrorCode::kInvalidArgument, "request requires a nonempty application buffer");

    DeviceBuffer command_buffer;
    DeviceBuffer completion_buffer;
    RegisteredRegion data_region;
    RegisteredRegion command_region;
    RegisteredRegion completion_region;
    nds_protocol_completion pending{request.request_id, NDS_PROTOCOL_COMPLETION_PENDING, NDS_PROTOCOL_SUCCESS, 0U};
    nds_protocol_completion_wire completion_wire{};
    if (nds_protocol_completion_encode(&pending, &completion_wire) != 0) {
        return failure(ErrorCode::kProtocol, "invalid protocol record");
    }
    if (const auto result = connection->allocate(sizeof(nds_protocol_command_wire), &command_buffer); !result)
        return propagate(result.error());
    if (const auto result = connection->allocate(sizeof(completion_wire), &completion_buffer); !result)
        return propagate(result.error());
    if (const auto result = connection->copy_to_device(&completion_buffer, &completion_wire, sizeof(completion_wire));
        !result)
        return propagate(result.error());
    if (const auto result = connection->register_memory(request.data, &data_region); !result)
        return propagate(result.error());
    if (const auto result = connection->register_memory(&command_buffer, &command_region); !result)
        return propagate(result.error());
    if (const auto result = connection->register_memory(&completion_buffer, &completion_region); !result)
        return propagate(result.error());

    const auto completion_remote = connection->remote_region(completion_region);
    if (!completion_remote)
        return propagate(completion_remote.error());
    const auto capacity = exchange_bootstrap(connection, *completion_remote);
    if (!capacity)
        return propagate(capacity.error());
    if (request.offset > *capacity || request.length > *capacity - request.offset) {
        return failure(ErrorCode::kProtocol, "requested storage range exceeds server namespace capacity");
    }

    const std::uint32_t remote_access =
        request.operation == NDS_PROTOCOL_READ ? NDS_PROTOCOL_ACCESS_REMOTE_WRITE : NDS_PROTOCOL_ACCESS_REMOTE_READ;
    const auto data_remote = connection->remote_region(data_region);
    if (!data_remote)
        return propagate(data_remote.error());
    const nds_protocol_memory remote_data{data_remote->address, data_remote->length, data_remote->key, remote_access};
    const nds_protocol_command command{request.request_id, request.operation, request.offset, request.length,
                                       remote_data};
    nds_protocol_command_wire command_wire{};
    if (nds_protocol_command_encode(&command, &command_wire) != 0) {
        return failure(ErrorCode::kProtocol, "invalid protocol record");
    }
    if (const auto result = connection->copy_to_device(&command_buffer, &command_wire, sizeof(command_wire)); !result)
        return propagate(result.error());
    if (const auto result = connection->ready(); !result)
        return propagate(result.error());
    if (const auto result = connection->send(command_region, sizeof(command_wire)); !result)
        return propagate(result.error());
    return wait_for_completion(connection, completion_buffer, request.request_id, request.length);
}

}  // namespace nds::client
