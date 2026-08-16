#include "nds/device_storage.h"

#include <cassert>
#include <cstring>

int main() {
    assert(sizeof(nds_device_storage) == 304U);
    assert(sizeof(nds_device_storage_io) == 40U);
    assert(sizeof(nds_device_storage_request) == 360U);

    nds_device_storage storage{};
    nds_device_storage_io io{};
    assert(!nds_device_storage_valid(&storage, &io));

    storage.abi_version = NDS_DEVICE_STORAGE_ABI_VERSION;
    storage.size = sizeof(storage);
    storage.connection.abi_version = NDS_DEVICE_CONNECTION_ABI_VERSION;
    storage.connection.size = sizeof(storage.connection);
    storage.request_id = 1U;
    storage.capacity = 4096U;
    storage.command = {0x1000U, sizeof(nds_protocol_command_wire), 2U};
    storage.completion = {0x2000U, sizeof(nds_protocol_completion_wire), 3U};
    io.operation = NDS_PROTOCOL_WRITE;
    io.length = 64U;
    io.offset = 0U;
    io.data = {0x3000U, 64U, 4U};
    io.data_rkey = 5U;
    assert(nds_device_storage_valid(&storage, &io));

    nds_protocol_command_wire command{};
    nds_protocol_completion_wire pending{};
    nds_device_storage_encode_command(&storage, &io, &command);
    nds_device_storage_encode_pending(&storage, &pending);
    assert(nds_device_htonl(command.magic) == NDS_PROTOCOL_COMMAND_MAGIC);
    assert(nds_device_htonll(command.request_id) == 1U);
    assert(nds_device_htonl(command.data_rkey) == 5U);
    assert(!nds_device_storage_completion_done(&pending, 1U, 64U));

    pending.state = nds_device_htons(NDS_PROTOCOL_COMPLETION_COMPLETE);
    pending.bytes_transferred = nds_device_htonll(64U);
    assert(nds_device_storage_completion_done(&pending, 1U, 64U));
    return 0;
}
