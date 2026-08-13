#include "nds/storage_protocol.h"

#include <arpa/inet.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

namespace {

uint64_t host_to_be64(uint64_t value)
{
    const uint32_t high = htonl(static_cast<uint32_t>(value >> 32));
    const uint32_t low = htonl(static_cast<uint32_t>(value));
    return (static_cast<uint64_t>(low) << 32) | high;
}

uint64_t be64_to_host(uint64_t value)
{
    const uint32_t high = ntohl(static_cast<uint32_t>(value >> 32));
    const uint32_t low = ntohl(static_cast<uint32_t>(value));
    return (static_cast<uint64_t>(low) << 32) | high;
}

void set_error(char error[NDS_STORAGE_ERROR_CAPACITY], const char *format, ...)
{
    if (error == nullptr) return;
    va_list arguments;
    va_start(arguments, format);
    (void)vsnprintf(error, NDS_STORAGE_ERROR_CAPACITY, format, arguments);
    va_end(arguments);
}

void clear_error(char error[NDS_STORAGE_ERROR_CAPACITY])
{
    if (error != nullptr) error[0] = '\0';
}

int validate_memory(const nds_storage_memory *memory, uint32_t required_access,
                    char error[NDS_STORAGE_ERROR_CAPACITY])
{
    if (memory == nullptr || memory->address == 0U || memory->length == 0U || memory->rkey == 0U ||
        (memory->access & required_access) != required_access) {
        set_error(error, "memory descriptor requires address, length, rkey, and requested access");
        return -1;
    }
    return 0;
}

int validate_bootstrap(const nds_storage_bootstrap *bootstrap, char error[NDS_STORAGE_ERROR_CAPACITY])
{
    if (bootstrap == nullptr || bootstrap->namespace_capacity == 0U ||
        validate_memory(&bootstrap->completion, NDS_STORAGE_ACCESS_REMOTE_WRITE, error) != 0 ||
        bootstrap->completion.length < sizeof(nds_storage_completion_wire)) {
        if (bootstrap != nullptr && bootstrap->namespace_capacity == 0U) set_error(error, "namespace capacity must be nonzero");
        return -1;
    }
    return 0;
}

int validate_command(const nds_storage_command *command, char error[NDS_STORAGE_ERROR_CAPACITY])
{
    uint32_t required_access;
    if (command == nullptr || command->request_id == 0U || command->length == 0U ||
        (command->operation != NDS_STORAGE_READ && command->operation != NDS_STORAGE_WRITE)) {
        set_error(error, "command requires request ID, Read or Write operation, and nonzero length");
        return -1;
    }
    required_access = command->operation == NDS_STORAGE_READ ? NDS_STORAGE_ACCESS_REMOTE_WRITE : NDS_STORAGE_ACCESS_REMOTE_READ;
    if (command->data.length < command->length || validate_memory(&command->data, required_access, error) != 0) return -1;
    return 0;
}

int validate_completion(const nds_storage_completion *completion, char error[NDS_STORAGE_ERROR_CAPACITY])
{
    if (completion == nullptr || completion->request_id == 0U ||
        (completion->state != NDS_STORAGE_COMPLETION_PENDING && completion->state != NDS_STORAGE_COMPLETION_COMPLETE) ||
        completion->status > NDS_STORAGE_INTERNAL_ERROR) {
        set_error(error, "invalid completion record");
        return -1;
    }
    if (completion->state == NDS_STORAGE_COMPLETION_PENDING &&
        (completion->status != NDS_STORAGE_SUCCESS || completion->bytes_transferred != 0U)) {
        set_error(error, "pending completion must have success status and zero transferred bytes");
        return -1;
    }
    return 0;
}

} // namespace

int nds_storage_bootstrap_encode(const nds_storage_bootstrap *bootstrap, nds_storage_bootstrap_wire *wire,
                                 char error[NDS_STORAGE_ERROR_CAPACITY])
{
    if (wire == nullptr || validate_bootstrap(bootstrap, error) != 0) return -1;
    *wire = {};
    wire->magic = htonl(NDS_STORAGE_BOOTSTRAP_MAGIC);
    wire->version = htons(NDS_STORAGE_PROTOCOL_VERSION);
    wire->namespace_capacity = host_to_be64(bootstrap->namespace_capacity);
    wire->completion_address = host_to_be64(bootstrap->completion.address);
    wire->completion_length = host_to_be64(bootstrap->completion.length);
    wire->completion_rkey = htonl(bootstrap->completion.rkey);
    wire->completion_access = htonl(bootstrap->completion.access);
    clear_error(error);
    return 0;
}

int nds_storage_bootstrap_decode(const nds_storage_bootstrap_wire *wire, nds_storage_bootstrap *bootstrap,
                                 char error[NDS_STORAGE_ERROR_CAPACITY])
{
    if (wire == nullptr || bootstrap == nullptr || ntohl(wire->magic) != NDS_STORAGE_BOOTSTRAP_MAGIC ||
        ntohs(wire->version) != NDS_STORAGE_PROTOCOL_VERSION) {
        set_error(error, "invalid storage bootstrap header");
        return -1;
    }
    *bootstrap = {be64_to_host(wire->namespace_capacity),
                  {be64_to_host(wire->completion_address), be64_to_host(wire->completion_length),
                   ntohl(wire->completion_rkey), ntohl(wire->completion_access)}};
    return validate_bootstrap(bootstrap, error);
}

int nds_storage_command_encode(const nds_storage_command *command, nds_storage_command_wire *wire,
                               char error[NDS_STORAGE_ERROR_CAPACITY])
{
    if (wire == nullptr || validate_command(command, error) != 0) return -1;
    *wire = {};
    wire->magic = htonl(NDS_STORAGE_COMMAND_MAGIC);
    wire->version = htons(NDS_STORAGE_PROTOCOL_VERSION);
    wire->operation = htons(command->operation);
    wire->request_id = host_to_be64(command->request_id);
    wire->offset = host_to_be64(command->offset);
    wire->length = host_to_be64(command->length);
    wire->data_address = host_to_be64(command->data.address);
    wire->data_length = host_to_be64(command->data.length);
    wire->data_rkey = htonl(command->data.rkey);
    wire->data_access = htonl(command->data.access);
    clear_error(error);
    return 0;
}

int nds_storage_command_decode(const nds_storage_command_wire *wire, nds_storage_command *command,
                               char error[NDS_STORAGE_ERROR_CAPACITY])
{
    if (wire == nullptr || command == nullptr || ntohl(wire->magic) != NDS_STORAGE_COMMAND_MAGIC ||
        ntohs(wire->version) != NDS_STORAGE_PROTOCOL_VERSION) {
        set_error(error, "invalid storage command header");
        return -1;
    }
    *command = {be64_to_host(wire->request_id), ntohs(wire->operation), be64_to_host(wire->offset),
                be64_to_host(wire->length), {be64_to_host(wire->data_address), be64_to_host(wire->data_length),
                ntohl(wire->data_rkey), ntohl(wire->data_access)}};
    return validate_command(command, error);
}

int nds_storage_completion_encode(const nds_storage_completion *completion, nds_storage_completion_wire *wire,
                                  char error[NDS_STORAGE_ERROR_CAPACITY])
{
    if (wire == nullptr || validate_completion(completion, error) != 0) return -1;
    *wire = {};
    wire->magic = htonl(NDS_STORAGE_COMPLETION_MAGIC);
    wire->version = htons(NDS_STORAGE_PROTOCOL_VERSION);
    wire->state = htons(completion->state);
    wire->status = htons(completion->status);
    wire->request_id = host_to_be64(completion->request_id);
    wire->bytes_transferred = host_to_be64(completion->bytes_transferred);
    clear_error(error);
    return 0;
}

int nds_storage_completion_decode(const nds_storage_completion_wire *wire, nds_storage_completion *completion,
                                  char error[NDS_STORAGE_ERROR_CAPACITY])
{
    if (wire == nullptr || completion == nullptr || ntohl(wire->magic) != NDS_STORAGE_COMPLETION_MAGIC ||
        ntohs(wire->version) != NDS_STORAGE_PROTOCOL_VERSION) {
        set_error(error, "invalid storage completion header");
        return -1;
    }
    *completion = {be64_to_host(wire->request_id), ntohs(wire->state), ntohs(wire->status),
                   be64_to_host(wire->bytes_transferred)};
    return validate_completion(completion, error);
}
