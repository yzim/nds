#include "protocol.hh"

#include "nds/protocol.h"

namespace nds::server {
namespace {

Result<nds_protocol_bootstrap> exchange_bootstrap(Connection *connection, std::uint64_t capacity) {
    nds_protocol_bootstrap_wire bootstrap_wire{};
    nds_protocol_namespace_wire namespace_wire{};
    const nds_protocol_namespace namespace_record{capacity};
    if (const auto received = connection->bootstrap()->receive_bytes(&bootstrap_wire, sizeof(bootstrap_wire));
        !received)
        return propagate(received.error());
    nds_protocol_bootstrap bootstrap{};
    if (nds_protocol_bootstrap_decode(&bootstrap_wire, &bootstrap) != 0 ||
        nds_protocol_namespace_encode(&namespace_record, &namespace_wire) != 0) {
        return failure(ErrorCode::kProtocol, "invalid protocol record");
    }
    if (const auto sent = connection->bootstrap()->send_bytes(&namespace_wire, sizeof(namespace_wire)); !sent)
        return propagate(sent.error());
    return success(bootstrap);
}

}  // namespace

Result<void> serve_request(Connection *connection, std::vector<unsigned char> *storage, std::uint32_t timeout_ms) {
    if (connection == nullptr || storage == nullptr)
        return failure(ErrorCode::kInvalidArgument, "connection and namespace are required");
    nds_protocol_command_wire command_wire{};
    nds_protocol_completion_wire completion_wire{};
    RegisteredRegion command_region;
    RegisteredRegion completion_region;
    if (const auto result = connection->prepare_receive(&command_wire, sizeof(command_wire), &command_region); !result)
        return propagate(result.error());
    if (const auto result = connection->register_memory(&completion_wire, sizeof(completion_wire),
                                                        MemoryAccess::LocalRead, &completion_region);
        !result)
        return propagate(result.error());
    if (const auto result = connection->activate(); !result)
        return propagate(result.error());

    const auto bootstrap = exchange_bootstrap(connection, storage->size());
    if (!bootstrap)
        return propagate(bootstrap.error());
    if (const auto result = connection->receive(timeout_ms); !result)
        return propagate(result.error());

    nds_protocol_command command{};
    if (nds_protocol_command_decode(&command_wire, &command) != 0) {
        return failure(ErrorCode::kProtocol, "invalid protocol record");
    }
    nds_protocol_completion completion{command.request_id, NDS_PROTOCOL_COMPLETION_COMPLETE, NDS_PROTOCOL_SUCCESS,
                                       command.length};
    if (command.offset > storage->size() || command.length > storage->size() - command.offset) {
        completion.status = NDS_PROTOCOL_RANGE_ERROR;
        completion.bytes_transferred = 0U;
    } else {
        RegisteredRegion data_region;
        auto *data = storage->data() + command.offset;
        if (const auto result =
                connection->register_memory(data, command.length, MemoryAccess::LocalWrite, &data_region);
            !result) {
            return propagate(result.error());
        }
        const auto transferred =
            command.operation == NDS_PROTOCOL_READ
                ? connection->write(data_region, command.data.address, command.data.rkey, command.length)
                : connection->read(data_region, command.data.address, command.data.rkey, command.length);
        if (!transferred)
            return propagate(transferred.error());
    }
    if (nds_protocol_completion_encode(&completion, &completion_wire) != 0) {
        return failure(ErrorCode::kProtocol, "invalid protocol record");
    }
    return connection->write(completion_region, bootstrap->completion.address, bootstrap->completion.rkey,
                             sizeof(completion_wire));
}

}  // namespace nds::server
