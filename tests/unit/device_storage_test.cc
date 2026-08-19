#include "nds/device_storage.h"

#include <gtest/gtest.h>
#include <cstdint>
#include <cstring>

namespace {

nds_device_storage valid_storage() {
    nds_device_storage storage{};
    storage.abi_version = NDS_DEVICE_STORAGE_ABI_VERSION;
    storage.size = sizeof(storage);
    storage.transport.abi_version = NDS_DEVICE_TRANSPORT_ABI_VERSION;
    storage.transport.size = sizeof(storage.transport);
    storage.request_id = 1U;
    storage.capacity = 4096U;
    storage.command = {0x1000U, sizeof(nds_protocol_command_wire), 2U};
    storage.completion = {0x2000U, sizeof(nds_protocol_completion_wire), 3U};
    return storage;
}

nds_device_storage_io valid_io(std::uint16_t operation) {
    nds_device_storage_io io{};
    io.operation = operation;
    io.length = 64U;
    io.offset = 0U;
    io.data = {0x3000U, 64U, 4U};
    io.data_rkey = 5U;
    return io;
}

}  // namespace

TEST(DeviceStorageTest, ValidatesAndEncodesRequests) {
    EXPECT_TRUE(sizeof(nds_device_storage) == 304U);
    EXPECT_TRUE(sizeof(nds_device_storage_io) == 40U);
    EXPECT_TRUE(sizeof(nds_device_storage_request) == 360U);

    nds_device_storage storage{};
    nds_device_storage_io io{};
    EXPECT_TRUE(!nds_device_storage_valid(&storage, &io));

    storage = valid_storage();
    io = valid_io(NDS_PROTOCOL_WRITE);
    EXPECT_TRUE(nds_device_storage_valid(&storage, &io));
    io = valid_io(NDS_PROTOCOL_READ);
    EXPECT_TRUE(nds_device_storage_valid(&storage, &io));

    io = valid_io(NDS_PROTOCOL_WRITE);
    io.offset = 4096U;
    EXPECT_TRUE(!nds_device_storage_valid(&storage, &io));
    io = valid_io(NDS_PROTOCOL_WRITE);
    io.length = 4097U;
    io.data.length = 4097U;
    EXPECT_TRUE(!nds_device_storage_valid(&storage, &io));
    io = valid_io(0U);
    EXPECT_TRUE(!nds_device_storage_valid(&storage, &io));
    io = valid_io(NDS_PROTOCOL_WRITE);
    io.data_rkey = 0U;
    EXPECT_TRUE(!nds_device_storage_valid(&storage, &io));

    storage = valid_storage();
    io = valid_io(NDS_PROTOCOL_WRITE);
    nds_protocol_command_wire write_command{};
    nds_protocol_completion_wire pending{};
    nds_device_storage_encode_command(&storage, &io, &write_command);
    nds_device_storage_encode_pending(&storage, &pending);
    EXPECT_TRUE(nds_device_htonl(write_command.magic) == NDS_PROTOCOL_COMMAND_MAGIC);
    EXPECT_TRUE(nds_device_htons(write_command.operation) == NDS_PROTOCOL_WRITE);
    EXPECT_TRUE(nds_device_htonll(write_command.request_id) == 1U);
    EXPECT_TRUE(nds_device_htonl(write_command.data_rkey) == 5U);
    EXPECT_TRUE(nds_device_htonl(write_command.data_access) == NDS_PROTOCOL_ACCESS_REMOTE_READ);
    EXPECT_TRUE(!nds_device_storage_completion_done(&pending, 1U, 64U));

    io = valid_io(NDS_PROTOCOL_READ);
    nds_protocol_command_wire read_command{};
    nds_device_storage_encode_command(&storage, &io, &read_command);
    EXPECT_TRUE(nds_device_htons(read_command.operation) == NDS_PROTOCOL_READ);
    EXPECT_TRUE(nds_device_htonl(read_command.data_access) == NDS_PROTOCOL_ACCESS_REMOTE_WRITE);

    pending.state = nds_device_htons(NDS_PROTOCOL_COMPLETION_COMPLETE);
    pending.bytes_transferred = nds_device_htonll(64U);
    EXPECT_TRUE(nds_device_storage_completion_done(&pending, 1U, 64U));
    EXPECT_TRUE(!nds_device_storage_completion_done(&pending, 2U, 64U));
    EXPECT_TRUE(!nds_device_storage_completion_done(&pending, 1U, 32U));

    nds_device_storage_request request{};
    request.abi_version = NDS_DEVICE_STORAGE_ABI_VERSION;
    request.size = sizeof(request);
    request.storage = valid_storage();
    request.io = valid_io(NDS_PROTOCOL_WRITE);
    request.operation_result_address = 0x4000U;
    EXPECT_TRUE(request.size == 360U);
    EXPECT_TRUE(nds_device_storage_valid(&request.storage, &request.io));
}
