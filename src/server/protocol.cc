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
        return unexpected(received.error());
    nds_protocol_bootstrap bootstrap{};
    if (nds_protocol_bootstrap_decode(&bootstrap_wire, &bootstrap) != 0 ||
        nds_protocol_namespace_encode(&namespace_record, &namespace_wire) != 0) {
        return unexpected(ErrorCode::kProtocol, "invalid protocol record");
    }
    if (const auto sent = connection->bootstrap()->send_bytes(&namespace_wire, sizeof(namespace_wire)); !sent)
        return unexpected(sent.error());
    return bootstrap;
}

}  // namespace

Result<void> serve_requests(Connection *connection, std::vector<unsigned char> *storage, std::uint32_t request_count,
                            std::uint32_t timeout_ms) {
    if (connection == nullptr || storage == nullptr || request_count == 0U)
        return unexpected(ErrorCode::kInvalidArgument, "connection and namespace are required");
    nds_protocol_command_wire command_wire{};
    nds_protocol_completion_wire completion_wire{};
    auto completion_region =
        connection->register_memory(&completion_wire, sizeof(completion_wire), MemoryAccess::LocalRead);
    if (!completion_region)
        return unexpected(completion_region.error());
    if (const auto result = connection->activate(); !result)
        return unexpected(result.error());

    const auto bootstrap = exchange_bootstrap(connection, storage->size());
    if (!bootstrap)
        return unexpected(bootstrap.error());
    for (std::uint32_t request_index = 0U; request_index < request_count; ++request_index) {
        auto command_region = connection->prepare_receive(&command_wire, sizeof(command_wire));
        if (!command_region)
            return unexpected(command_region.error());
        if (const auto result = connection->receive(timeout_ms); !result)
            return unexpected(result.error());

        nds_protocol_command command{};
        if (nds_protocol_command_decode(&command_wire, &command) != 0)
            return unexpected(ErrorCode::kProtocol, "invalid protocol record");
        nds_protocol_completion completion{command.request_id, NDS_PROTOCOL_COMPLETION_COMPLETE, NDS_PROTOCOL_SUCCESS,
                                           command.length};
        if (command.offset > storage->size() || command.length > storage->size() - command.offset) {
            completion.status = NDS_PROTOCOL_RANGE_ERROR;
            completion.bytes_transferred = 0U;
        } else {
            auto *data = storage->data() + command.offset;
            auto data_region = connection->register_memory(data, command.length, MemoryAccess::LocalWrite);
            if (!data_region)
                return unexpected(data_region.error());
            const auto transferred =
                command.operation == NDS_PROTOCOL_READ
                    ? connection->write(*data_region, command.data.address, command.data.rkey, command.length)
                    : connection->read(*data_region, command.data.address, command.data.rkey, command.length);
            if (!transferred)
                return unexpected(transferred.error());
        }
        if (nds_protocol_completion_encode(&completion, &completion_wire) != 0)
            return unexpected(ErrorCode::kProtocol, "invalid protocol record");
        if (const auto completed = connection->write(*completion_region, bootstrap->completion.address,
                                                     bootstrap->completion.rkey, sizeof(completion_wire));
            !completed)
            return unexpected(completed.error());
    }
    return {};
}

}  // namespace nds::server
