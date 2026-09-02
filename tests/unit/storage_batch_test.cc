#include "storage.hh"

#include <gtest/gtest.h>

#include <cstdint>
#include <span>

namespace nds::client {

struct StorageClientTestAccess {
    static void mark_open(StorageClient *client, std::uint64_t capacity) {
        client->capacity_ = capacity;
        client->opened_ = true;
    }

    static void set_slot_count(StorageClient *client, std::size_t count) {
        client->slots_.resize(count);
        client->pending_.resize(count);
        client->next_slot_ = 0U;
    }

    static void mark_pending(StorageClient *client, std::size_t slot_index, std::uint64_t command_id) {
        client->pending_[slot_index] = StorageClient::PendingRequest{
            .command_id = command_id,
            .expected_bytes = 1U,
            .data_regions = {},
            .descriptor_buffer = {},
            .descriptor_region = {},
        };
    }

    static void release(StorageClient *client, std::size_t slot_index) {
        client->pending_[slot_index].reset();
    }

    static Result<std::uint32_t> acquire_slot(StorageClient *client) {
        return client->acquire_slot();
    }
};

}  // namespace nds::client

TEST(StorageBatchTest, RejectsEmptyBatch) {
    nds::client::StorageClient client;
    nds::client::StorageClientTestAccess::mark_open(&client, 1024U);
    const std::span<const nds::client::StorageIo> requests;

    EXPECT_FALSE(client.read_batch(requests).ok());
    EXPECT_FALSE(client.write_batch(requests).ok());
}

TEST(StorageBatchTest, RejectsMalformedDescriptorBeforeSubmission) {
    nds::client::StorageClient client;
    nds::client::StorageClientTestAccess::mark_open(&client, 1024U);
    nds::client::MemoryBuffer buffer;
    const nds::client::StorageIo request{0U, &buffer, 1U};

    EXPECT_FALSE(client.read_batch(std::span{&request, 1U}).ok());
    EXPECT_FALSE(client.write_batch(std::span{&request, 1U}).ok());
}

TEST(StorageBatchTest, SchedulesCommandsAcrossAvailableSlots) {
    nds::client::StorageClient client;
    nds::client::StorageClientTestAccess::mark_open(&client, 1024U);
    nds::client::StorageClientTestAccess::set_slot_count(&client, 3U);

    const auto first = nds::client::StorageClientTestAccess::acquire_slot(&client);
    ASSERT_TRUE(first.ok());
    EXPECT_EQ(first.value(), 0U);
    nds::client::StorageClientTestAccess::mark_pending(&client, first.value(), 1U);

    const auto second = nds::client::StorageClientTestAccess::acquire_slot(&client);
    ASSERT_TRUE(second.ok());
    EXPECT_EQ(second.value(), 1U);
    nds::client::StorageClientTestAccess::mark_pending(&client, second.value(), 2U);

    const auto third = nds::client::StorageClientTestAccess::acquire_slot(&client);
    ASSERT_TRUE(third.ok());
    EXPECT_EQ(third.value(), 2U);
    nds::client::StorageClientTestAccess::mark_pending(&client, third.value(), 3U);

    EXPECT_FALSE(nds::client::StorageClientTestAccess::acquire_slot(&client).ok());
    nds::client::StorageClientTestAccess::release(&client, second.value());
    const auto reused = nds::client::StorageClientTestAccess::acquire_slot(&client);
    ASSERT_TRUE(reused.ok());
    EXPECT_EQ(reused.value(), second.value());
}
