#include "backend_storage.h"

#include <gtest/gtest.h>

namespace {

NdsStorageDescriptor valid_context() {
    static NdsQpDescriptor qp_descriptors[1]{};
    static NdsTransportQpState qp_states[1]{};
    static NdsStorageSlotDescriptor slot_descriptors[1]{};
    qp_descriptors[0] = {};
    qp_states[0] = {};
    slot_descriptors[0] = NdsStorageSlotDescriptor{
        .command_buffer = {0x1000U, nds::kStorageCommandBytes, 2U},
        .completion_buffer = {0x2000U, nds::kStorageCompletionBytes, 3U},
        .qp_index = 0U,
        .reserved = 0U,
    };
    NdsStorageDescriptor context{};
    context.transport = NdsTransportDescriptor{
        .qp_descriptors_address = reinterpret_cast<uint64_t>(qp_descriptors),
        .qp_states_address = reinterpret_cast<uint64_t>(qp_states),
        .qp_count = 1U,
        .reserved = 0U,
    };
    context.slot_descriptors_address = reinterpret_cast<uint64_t>(slot_descriptors);
    context.slot_count = 1U;
    context.reserved = 0U;
    context.capacity = 4096U;
    return context;
}

}  // namespace

TEST(DeviceStorageTest, ValidatesSingleOperationCommands) {
    NdsStorageDescriptor context = valid_context();
    nds::StorageReadCommand read{
        .command_id = 1U,
        .offset = 0U,
        .length = 64U,
        .data = {0x3000U, 64U, 4U},
        .slot_index = 0U,
    };
    nds::StorageWriteCommand write{
        .command_id = 1U,
        .offset = 0U,
        .length = 64U,
        .data = {0x3000U, 64U, 4U},
        .slot_index = 0U,
    };

    EXPECT_TRUE(nds_storage_read_valid(&context, &read));
    EXPECT_TRUE(nds_storage_write_valid(&context, &write));

    read.offset = context.capacity;
    EXPECT_FALSE(nds_storage_read_valid(&context, &read));
    write.length = context.capacity + 1U;
    write.data.length = write.length;
    EXPECT_FALSE(nds_storage_write_valid(&context, &write));

    context.slot_descriptors_address = 0U;
    EXPECT_FALSE(nds_storage_read_valid(&context, &read));

    context = valid_context();
    context.transport.reserved = 1U;
    EXPECT_FALSE(nds_storage_read_valid(&context, &read));

    context = valid_context();
    read.slot_index = 1U;
    EXPECT_FALSE(nds_storage_read_valid(&context, &read));
}

TEST(DeviceStorageTest, ValidatesBatchOperationCommands) {
    const NdsStorageDescriptor context = valid_context();
    nds::StorageBatchReadCommand read{
        .command_id = 1U,
        .entry_count = 2U,
        .total_length = 128U,
        .entries = {0x3000U, 2U * nds::kStorageBatchEntryBytes, 4U},
        .slot_index = 0U,
    };
    nds::StorageBatchWriteCommand write{
        .command_id = 1U,
        .entry_count = 2U,
        .total_length = 128U,
        .entries = {0x3000U, 2U * nds::kStorageBatchEntryBytes, 4U},
        .slot_index = 0U,
    };

    EXPECT_TRUE(nds_storage_batch_read_valid(&context, &read));
    EXPECT_TRUE(nds_storage_batch_write_valid(&context, &write));

    read.entry_count = 0U;
    EXPECT_FALSE(nds_storage_batch_read_valid(&context, &read));
    write.entries.length = nds::kStorageBatchEntryBytes;
    EXPECT_FALSE(nds_storage_batch_write_valid(&context, &write));
}

TEST(DeviceStorageTest, ValidatesCompletionWait) {
    NdsStorageDescriptor context = valid_context();

    EXPECT_TRUE(nds_storage_wait_valid(&context, 1U, 64U, 0U));
    EXPECT_FALSE(nds_storage_wait_valid(&context, 0U, 64U, 0U));
    EXPECT_FALSE(nds_storage_wait_valid(&context, 1U, 0U, 0U));
    EXPECT_FALSE(nds_storage_wait_valid(&context, 1U, 64U, 1U));
    context.slot_descriptors_address = 0U;
    EXPECT_FALSE(nds_storage_wait_valid(&context, 1U, 64U, 0U));
}

TEST(DeviceStorageTest, ValidatesSlotQueueMappingAndReservedBits) {
    NdsStorageDescriptor context = valid_context();
    NdsStorageSlotDescriptor *slot =
        reinterpret_cast<NdsStorageSlotDescriptor *>(static_cast<uintptr_t>(context.slot_descriptors_address));

    slot->qp_index = 1U;
    EXPECT_FALSE(nds_storage_wait_valid(&context, 1U, 64U, 0U));
    slot->qp_index = 0U;
    slot->reserved = 1U;
    EXPECT_FALSE(nds_storage_wait_valid(&context, 1U, 64U, 0U));
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
