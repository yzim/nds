#include "backend_storage.h"

#include <gtest/gtest.h>

namespace {

NdsStorageContext valid_context() {
    NdsStorageContext context{};
    context.command_buffer = {0x1000U, nds::kStorageCommandBytes, 2U};
    context.completion = {0x2000U, nds::kStorageCompletionBytes, 3U};
    context.capacity = 4096U;
    return context;
}

}  // namespace

TEST(DeviceStorageTest, ValidatesSingleOperationCommands) {
    NdsStorageContext context = valid_context();
    nds::StorageReadCommand read{1U, 0U, 64U, {0x3000U, 64U, 4U}};
    nds::StorageWriteCommand write{1U, 0U, 64U, {0x3000U, 64U, 4U}};

    EXPECT_TRUE(nds_storage_read_valid(&context, &read));
    EXPECT_TRUE(nds_storage_write_valid(&context, &write));

    read.offset = context.capacity;
    EXPECT_FALSE(nds_storage_read_valid(&context, &read));
    write.length = context.capacity + 1U;
    write.data.length = write.length;
    EXPECT_FALSE(nds_storage_write_valid(&context, &write));

    context.command_buffer.length = nds::kStorageCommandBytes - 1U;
    EXPECT_FALSE(nds_storage_read_valid(&context, &read));
}

TEST(DeviceStorageTest, ValidatesBatchOperationCommands) {
    const NdsStorageContext context = valid_context();
    nds::StorageBatchReadCommand read{1U, 2U, 128U, {0x3000U, 2U * nds::kStorageBatchEntryBytes, 4U}};
    nds::StorageBatchWriteCommand write{1U, 2U, 128U, {0x3000U, 2U * nds::kStorageBatchEntryBytes, 4U}};

    EXPECT_TRUE(nds_storage_batch_read_valid(&context, &read));
    EXPECT_TRUE(nds_storage_batch_write_valid(&context, &write));

    read.entry_count = 0U;
    EXPECT_FALSE(nds_storage_batch_read_valid(&context, &read));
    write.entries.length = nds::kStorageBatchEntryBytes;
    EXPECT_FALSE(nds_storage_batch_write_valid(&context, &write));
}

TEST(DeviceStorageTest, ValidatesCompletionWait) {
    NdsStorageContext context = valid_context();

    EXPECT_TRUE(nds_storage_wait_valid(&context, 1U, 64U));
    EXPECT_FALSE(nds_storage_wait_valid(&context, 0U, 64U));
    EXPECT_FALSE(nds_storage_wait_valid(&context, 1U, 0U));
    context.completion.length = nds::kStorageCompletionBytes - 1U;
    EXPECT_FALSE(nds_storage_wait_valid(&context, 1U, 64U));
}

TEST(DeviceStorageTest, DefinesDirectOperatorResultEnvelopes) {
    constexpr std::int32_t kResult = -3;

    NdsStorageReadArgs read{};
    NdsStorageWriteArgs write{};
    NdsStorageBatchReadArgs batch_read{};
    NdsStorageBatchWriteArgs batch_write{};
    NdsStorageWaitArgs wait{};
    read.return_value = kResult;
    write.return_value = kResult;
    batch_read.return_value = kResult;
    batch_write.return_value = kResult;
    wait.return_value = kResult;

    EXPECT_EQ(read.return_value, kResult);
    EXPECT_EQ(write.return_value, kResult);
    EXPECT_EQ(batch_read.return_value, kResult);
    EXPECT_EQ(batch_write.return_value, kResult);
    EXPECT_EQ(wait.return_value, kResult);
}
