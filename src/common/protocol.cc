#include "nds/protocol.h"

#include <arpa/inet.h>
#include <string.h>

namespace {

uint64_t host_to_be64(uint64_t value) {
    const uint32_t high = htonl(static_cast<uint32_t>(value >> 32));
    const uint32_t low = htonl(static_cast<uint32_t>(value));
    return (static_cast<uint64_t>(low) << 32) | high;
}

uint64_t be64_to_host(uint64_t value) {
    const uint32_t high = ntohl(static_cast<uint32_t>(value >> 32));
    const uint32_t low = ntohl(static_cast<uint32_t>(value));
    return (static_cast<uint64_t>(low) << 32) | high;
}

enum nds_protocol_result validate_memory(const nds_protocol_memory *memory, uint32_t required_access) {
    if (memory == nullptr || memory->address == 0U || memory->length == 0U || memory->rkey == 0U ||
        (memory->access & required_access) != required_access) {
        return NDS_PROTOCOL_RESULT_INVALID_RECORD;
    }
    return NDS_PROTOCOL_RESULT_OK;
}

enum nds_protocol_result validate_bootstrap(const nds_protocol_bootstrap *bootstrap) {
    if (bootstrap == nullptr || validate_memory(&bootstrap->completion, NDS_PROTOCOL_ACCESS_REMOTE_WRITE) != 0 ||
        bootstrap->completion.length < sizeof(nds_protocol_completion_wire)) {
        return NDS_PROTOCOL_RESULT_INVALID_RECORD;
    }
    return NDS_PROTOCOL_RESULT_OK;
}

enum nds_protocol_result validate_command(const nds_protocol_command *command) {
    uint32_t required_access;
    if (command == nullptr || command->request_id == 0U || command->length == 0U ||
        (command->operation != NDS_PROTOCOL_READ && command->operation != NDS_PROTOCOL_WRITE)) {
        return NDS_PROTOCOL_RESULT_INVALID_RECORD;
    }
    required_access =
        command->operation == NDS_PROTOCOL_READ ? NDS_PROTOCOL_ACCESS_REMOTE_WRITE : NDS_PROTOCOL_ACCESS_REMOTE_READ;
    if (command->data.length < command->length || validate_memory(&command->data, required_access) != 0)
        return NDS_PROTOCOL_RESULT_INVALID_RECORD;
    return NDS_PROTOCOL_RESULT_OK;
}

enum nds_protocol_result validate_completion(const nds_protocol_completion *completion) {
    if (completion == nullptr || completion->request_id == 0U ||
        (completion->state != NDS_PROTOCOL_COMPLETION_PENDING &&
         completion->state != NDS_PROTOCOL_COMPLETION_COMPLETE) ||
        completion->status > NDS_PROTOCOL_INTERNAL_ERROR) {
        return NDS_PROTOCOL_RESULT_INVALID_RECORD;
    }
    if (completion->state == NDS_PROTOCOL_COMPLETION_PENDING &&
        (completion->status != NDS_PROTOCOL_SUCCESS || completion->bytes_transferred != 0U)) {
        return NDS_PROTOCOL_RESULT_INVALID_RECORD;
    }
    return NDS_PROTOCOL_RESULT_OK;
}

}  // namespace

enum nds_protocol_result nds_protocol_bootstrap_encode(const nds_protocol_bootstrap *bootstrap,
                                                       nds_protocol_bootstrap_wire *wire) {
    if (wire == nullptr || validate_bootstrap(bootstrap) != 0)
        return NDS_PROTOCOL_RESULT_INVALID_RECORD;
    *wire = {};
    wire->magic = htonl(NDS_PROTOCOL_BOOTSTRAP_MAGIC);
    wire->version = htons(NDS_PROTOCOL_VERSION);
    wire->completion_address = host_to_be64(bootstrap->completion.address);
    wire->completion_length = host_to_be64(bootstrap->completion.length);
    wire->completion_rkey = htonl(bootstrap->completion.rkey);
    wire->completion_access = htonl(bootstrap->completion.access);
    return NDS_PROTOCOL_RESULT_OK;
}

enum nds_protocol_result nds_protocol_bootstrap_decode(const nds_protocol_bootstrap_wire *wire,
                                                       nds_protocol_bootstrap *bootstrap) {
    if (wire == nullptr || bootstrap == nullptr || ntohl(wire->magic) != NDS_PROTOCOL_BOOTSTRAP_MAGIC ||
        ntohs(wire->version) != NDS_PROTOCOL_VERSION) {
        return NDS_PROTOCOL_RESULT_INVALID_RECORD;
    }
    *bootstrap = {{be64_to_host(wire->completion_address), be64_to_host(wire->completion_length),
                   ntohl(wire->completion_rkey), ntohl(wire->completion_access)}};
    return validate_bootstrap(bootstrap);
}

enum nds_protocol_result nds_protocol_namespace_encode(const nds_protocol_namespace *namespace_record,
                                                       nds_protocol_namespace_wire *wire) {
    if (namespace_record == nullptr || wire == nullptr || namespace_record->capacity == 0U) {
        return NDS_PROTOCOL_RESULT_INVALID_RECORD;
    }
    *wire = {};
    wire->magic = htonl(NDS_PROTOCOL_NAMESPACE_MAGIC);
    wire->version = htons(NDS_PROTOCOL_VERSION);
    wire->capacity = host_to_be64(namespace_record->capacity);
    return NDS_PROTOCOL_RESULT_OK;
}

enum nds_protocol_result nds_protocol_namespace_decode(const nds_protocol_namespace_wire *wire,
                                                       nds_protocol_namespace *namespace_record) {
    if (wire == nullptr || namespace_record == nullptr || ntohl(wire->magic) != NDS_PROTOCOL_NAMESPACE_MAGIC ||
        ntohs(wire->version) != NDS_PROTOCOL_VERSION) {
        return NDS_PROTOCOL_RESULT_INVALID_RECORD;
    }
    namespace_record->capacity = be64_to_host(wire->capacity);
    if (namespace_record->capacity == 0U) {
        return NDS_PROTOCOL_RESULT_INVALID_RECORD;
    }
    return NDS_PROTOCOL_RESULT_OK;
}

enum nds_protocol_result nds_protocol_command_encode(const nds_protocol_command *command,
                                                     nds_protocol_command_wire *wire) {
    if (wire == nullptr || validate_command(command) != 0)
        return NDS_PROTOCOL_RESULT_INVALID_RECORD;
    *wire = {};
    wire->magic = htonl(NDS_PROTOCOL_COMMAND_MAGIC);
    wire->version = htons(NDS_PROTOCOL_VERSION);
    wire->operation = htons(command->operation);
    wire->request_id = host_to_be64(command->request_id);
    wire->offset = host_to_be64(command->offset);
    wire->length = host_to_be64(command->length);
    wire->data_address = host_to_be64(command->data.address);
    wire->data_length = host_to_be64(command->data.length);
    wire->data_rkey = htonl(command->data.rkey);
    wire->data_access = htonl(command->data.access);
    return NDS_PROTOCOL_RESULT_OK;
}

enum nds_protocol_result nds_protocol_command_decode(const nds_protocol_command_wire *wire,
                                                     nds_protocol_command *command) {
    if (wire == nullptr || command == nullptr || ntohl(wire->magic) != NDS_PROTOCOL_COMMAND_MAGIC ||
        ntohs(wire->version) != NDS_PROTOCOL_VERSION) {
        return NDS_PROTOCOL_RESULT_INVALID_RECORD;
    }
    *command = {be64_to_host(wire->request_id),
                ntohs(wire->operation),
                be64_to_host(wire->offset),
                be64_to_host(wire->length),
                {be64_to_host(wire->data_address), be64_to_host(wire->data_length), ntohl(wire->data_rkey),
                 ntohl(wire->data_access)}};
    return validate_command(command);
}

enum nds_protocol_result nds_protocol_completion_encode(const nds_protocol_completion *completion,
                                                        nds_protocol_completion_wire *wire) {
    if (wire == nullptr || validate_completion(completion) != 0)
        return NDS_PROTOCOL_RESULT_INVALID_RECORD;
    *wire = {};
    wire->magic = htonl(NDS_PROTOCOL_COMPLETION_MAGIC);
    wire->version = htons(NDS_PROTOCOL_VERSION);
    wire->state = htons(completion->state);
    wire->status = htons(completion->status);
    wire->request_id = host_to_be64(completion->request_id);
    wire->bytes_transferred = host_to_be64(completion->bytes_transferred);
    return NDS_PROTOCOL_RESULT_OK;
}

enum nds_protocol_result nds_protocol_completion_decode(const nds_protocol_completion_wire *wire,
                                                        nds_protocol_completion *completion) {
    if (wire == nullptr || completion == nullptr || ntohl(wire->magic) != NDS_PROTOCOL_COMPLETION_MAGIC ||
        ntohs(wire->version) != NDS_PROTOCOL_VERSION) {
        return NDS_PROTOCOL_RESULT_INVALID_RECORD;
    }
    *completion = {be64_to_host(wire->request_id), ntohs(wire->state), ntohs(wire->status),
                   be64_to_host(wire->bytes_transferred)};
    return validate_completion(completion);
}
