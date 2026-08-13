#include "protocol.hh"

#include "nds/protocol.h"

#include <chrono>
#include <thread>

namespace nds::npu {
namespace {

constexpr std::uint32_t kCompletionTimeoutMs = 5000U;

bool exchange_bootstrap(Connection *connection, const RemoteRegion &completion, std::uint64_t *capacity,
                        std::string *error) {
    const nds_storage_bootstrap bootstrap{
        {completion.address, completion.length, completion.key, NDS_STORAGE_ACCESS_REMOTE_WRITE}};
    nds_storage_bootstrap_wire bootstrap_wire{};
    nds_storage_namespace_wire namespace_wire{};
    nds_storage_namespace storage_namespace{};
    char codec_error[NDS_STORAGE_ERROR_CAPACITY]{};
    if (nds_storage_bootstrap_encode(&bootstrap, &bootstrap_wire, codec_error) != 0 ||
        !connection->bootstrap()->send_bytes(&bootstrap_wire, sizeof(bootstrap_wire), error) ||
        !connection->bootstrap()->receive_bytes(&namespace_wire, sizeof(namespace_wire), error) ||
        nds_storage_namespace_decode(&namespace_wire, &storage_namespace, codec_error) != 0) {
        if (error->empty() && codec_error[0] != '\0')
            *error = codec_error;
        return false;
    }
    *capacity = storage_namespace.capacity;
    return true;
}

bool wait_for_completion(Connection *connection, const DeviceBuffer &buffer, std::uint64_t request_id,
                         std::uint64_t expected_bytes, std::string *error) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(kCompletionTimeoutMs);
    while (std::chrono::steady_clock::now() < deadline) {
        nds_storage_completion_wire wire{};
        nds_storage_completion completion{};
        char codec_error[NDS_STORAGE_ERROR_CAPACITY]{};
        if (!connection->copy_from_device(&wire, buffer, sizeof(wire), error))
            return false;
        if (nds_storage_completion_decode(&wire, &completion, codec_error) != 0) {
            *error = codec_error;
            return false;
        }
        if (completion.state == NDS_STORAGE_COMPLETION_COMPLETE) {
            if (completion.request_id != request_id || completion.status != NDS_STORAGE_SUCCESS ||
                completion.bytes_transferred != expected_bytes) {
                *error = "storage completion does not match the request";
                return false;
            }
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    *error = "timed out waiting for CPU storage completion";
    return false;
}

}  // namespace

bool execute_storage_request(Connection *connection, const StorageRequest &request, std::string *error) {
    if (connection == nullptr || request.data == nullptr || error == nullptr || request.length == 0U ||
        request.length > request.data->size())
        return false;

    DeviceBuffer command_buffer;
    DeviceBuffer completion_buffer;
    RegisteredRegion data_region;
    RegisteredRegion command_region;
    RegisteredRegion completion_region;
    nds_storage_completion pending{request.request_id, NDS_STORAGE_COMPLETION_PENDING, NDS_STORAGE_SUCCESS, 0U};
    nds_storage_completion_wire completion_wire{};
    char codec_error[NDS_STORAGE_ERROR_CAPACITY]{};
    if (nds_storage_completion_encode(&pending, &completion_wire, codec_error) != 0 ||
        !connection->allocate(sizeof(nds_storage_command_wire), &command_buffer, error) ||
        !connection->allocate(sizeof(completion_wire), &completion_buffer, error) ||
        !connection->copy_to_device(&completion_buffer, &completion_wire, sizeof(completion_wire), error) ||
        !connection->register_memory(request.data, &data_region, error) ||
        !connection->register_memory(&command_buffer, &command_region, error) ||
        !connection->register_memory(&completion_buffer, &completion_region, error)) {
        if (error->empty() && codec_error[0] != '\0')
            *error = codec_error;
        return false;
    }

    RemoteRegion completion_remote{};
    std::uint64_t capacity = 0U;
    if (!connection->remote_region(completion_region, &completion_remote, error) ||
        !exchange_bootstrap(connection, completion_remote, &capacity, error))
        return false;
    if (request.offset > capacity || request.length > capacity - request.offset) {
        *error = "requested storage range exceeds CPU namespace capacity";
        return false;
    }

    const std::uint32_t remote_access =
        request.operation == NDS_STORAGE_READ ? NDS_STORAGE_ACCESS_REMOTE_WRITE : NDS_STORAGE_ACCESS_REMOTE_READ;
    RemoteRegion data_remote{};
    if (!connection->remote_region(data_region, &data_remote, error))
        return false;
    const nds_storage_memory remote_data{data_remote.address, data_remote.length, data_remote.key, remote_access};
    const nds_storage_command command{request.request_id, request.operation, request.offset, request.length,
                                      remote_data};
    nds_storage_command_wire command_wire{};
    if (nds_storage_command_encode(&command, &command_wire, codec_error) != 0) {
        *error = codec_error;
        return false;
    }
    if (!connection->copy_to_device(&command_buffer, &command_wire, sizeof(command_wire), error) ||
        !connection->ready(error) || !connection->send(command_region, sizeof(command_wire), error))
        return false;
    return wait_for_completion(connection, completion_buffer, request.request_id, request.length, error);
}

}  // namespace nds::npu
