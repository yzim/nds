#include "protocol.hh"

#include "nds/protocol.h"

namespace nds::server {
namespace {

bool exchange_bootstrap(Connection *connection, std::uint64_t capacity, nds_protocol_bootstrap *bootstrap,
                        std::string *error) {
    nds_protocol_bootstrap_wire bootstrap_wire{};
    nds_protocol_namespace_wire namespace_wire{};
    const nds_protocol_namespace namespace_record{capacity};
    char codec_error[NDS_PROTOCOL_ERROR_CAPACITY]{};
    if (!connection->bootstrap()->receive_bytes(&bootstrap_wire, sizeof(bootstrap_wire), error) ||
        nds_protocol_bootstrap_decode(&bootstrap_wire, bootstrap, codec_error) != 0 ||
        nds_protocol_namespace_encode(&namespace_record, &namespace_wire, codec_error) != 0 ||
        !connection->bootstrap()->send_bytes(&namespace_wire, sizeof(namespace_wire), error)) {
        if (error->empty() && codec_error[0] != '\0')
            *error = codec_error;
        return false;
    }
    return true;
}

}  // namespace

bool serve_request(Connection *connection, std::vector<unsigned char> *storage, std::uint32_t timeout_ms,
                   std::string *error) {
    if (connection == nullptr || storage == nullptr || error == nullptr)
        return false;
    nds_protocol_command_wire command_wire{};
    nds_protocol_completion_wire completion_wire{};
    RegisteredRegion command_region;
    RegisteredRegion completion_region;
    if (!connection->prepare_receive(&command_wire, sizeof(command_wire), &command_region, error) ||
        !connection->register_memory(&completion_wire, sizeof(completion_wire), MemoryAccess::LocalRead,
                                     &completion_region, error) ||
        !connection->activate(error))
        return false;

    nds_protocol_bootstrap bootstrap{};
    if (!exchange_bootstrap(connection, storage->size(), &bootstrap, error) || !connection->receive(timeout_ms, error))
        return false;

    nds_protocol_command command{};
    char codec_error[NDS_PROTOCOL_ERROR_CAPACITY]{};
    if (nds_protocol_command_decode(&command_wire, &command, codec_error) != 0) {
        *error = codec_error;
        return false;
    }
    nds_protocol_completion completion{command.request_id, NDS_PROTOCOL_COMPLETION_COMPLETE, NDS_PROTOCOL_SUCCESS,
                                       command.length};
    if (command.offset > storage->size() || command.length > storage->size() - command.offset) {
        completion.status = NDS_PROTOCOL_RANGE_ERROR;
        completion.bytes_transferred = 0U;
    } else {
        RegisteredRegion data_region;
        auto *data = storage->data() + command.offset;
        if (!connection->register_memory(data, command.length, MemoryAccess::LocalWrite, &data_region, error)) {
            return false;
        }
        const bool transferred =
            command.operation == NDS_PROTOCOL_READ
                ? connection->write(data_region, command.data.address, command.data.rkey, command.length, error)
                : connection->read(data_region, command.data.address, command.data.rkey, command.length, error);
        if (!transferred)
            return false;
    }
    if (nds_protocol_completion_encode(&completion, &completion_wire, codec_error) != 0) {
        *error = codec_error;
        return false;
    }
    return connection->write(completion_region, bootstrap.completion.address, bootstrap.completion.rkey,
                             sizeof(completion_wire), error);
}

}  // namespace nds::server
