#include "nds/device_storage.h"

#include <gtest/gtest.h>

namespace {

NdsDeviceStorageContext valid_context() {
    NdsDeviceStorageContext context{};
    context.abi_version = NDS_DEVICE_STORAGE_ABI_VERSION;
    context.size = sizeof(context);
    context.transport.abi_version = NDS_DEVICE_TRANSPORT_ABI_VERSION;
    context.transport.size = sizeof(context.transport);
    context.command_buffer = {0x1000U, nds::kStorageCommandBytes, 2U};
    context.completion = {0x2000U, nds::kStorageCompletionBytes, 3U};
    context.capacity = 4096U;
    return context;
}

}  // namespace

TEST(DeviceStorageTest, ValidatesSingleOperationCommands) {
    NdsDeviceStorageContext context = valid_context();
    nds::StorageReadCommand read{1U, 0U, 64U, {0x3000U, 64U, 4U}};
    nds::StorageWriteCommand write{1U, 0U, 64U, {0x3000U, 64U, 4U}};

    EXPECT_TRUE(nds_device_storage_read_valid(&context, &read));
    EXPECT_TRUE(nds_device_storage_write_valid(&context, &write));

    read.offset = context.capacity;
    EXPECT_FALSE(nds_device_storage_read_valid(&context, &read));
    write.length = context.capacity + 1U;
    write.data.length = write.length;
    EXPECT_FALSE(nds_device_storage_write_valid(&context, &write));

    context.command_buffer.length = nds::kStorageCommandBytes - 1U;
    EXPECT_FALSE(nds_device_storage_read_valid(&context, &read));
}

TEST(DeviceStorageTest, ValidatesBatchOperationCommands) {
    const NdsDeviceStorageContext context = valid_context();
    nds::StorageBatchReadCommand read{1U, 2U, 128U, {0x3000U, 2U * nds::kStorageBatchEntryBytes, 4U}};
    nds::StorageBatchWriteCommand write{1U, 2U, 128U, {0x3000U, 2U * nds::kStorageBatchEntryBytes, 4U}};

    EXPECT_TRUE(nds_device_storage_batch_read_valid(&context, &read));
    EXPECT_TRUE(nds_device_storage_batch_write_valid(&context, &write));

    read.entry_count = 0U;
    EXPECT_FALSE(nds_device_storage_batch_read_valid(&context, &read));
    write.entries.length = nds::kStorageBatchEntryBytes;
    EXPECT_FALSE(nds_device_storage_batch_write_valid(&context, &write));
}
