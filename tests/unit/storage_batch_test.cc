#include "storage.hh"

#include <gtest/gtest.h>

namespace nds::client {

struct StorageClientTestAccess {
    static void mark_open(StorageClient *client, std::size_t count) {
        client->slots_.resize(count);
        client->allocated_slots_.assign(count, false);
        client->next_slot_ = 0U;
        client->opened_ = true;
        client->transport_ = reinterpret_cast<Transport *>(1U);
        client->storage_states_buffer_.location_ = MemoryLocation::Host;
        client->storage_states_buffer_.data_ = new std::byte[count * sizeof(NdsStorageState)];
        client->storage_states_buffer_.size_ = count * sizeof(NdsStorageState);
        for (std::size_t index = 0U; index < count; ++index)
            client->slots_[index].qp_index = static_cast<std::uint32_t>(index % 2U);
    }

    static Result<std::uint32_t> allocate(StorageClient *client) {
        return client->allocate_slot();
    }

    static void release_bookkeeping(StorageClient *client, std::uint32_t slot_id) {
        client->allocated_slots_[nds_storage_slot_id_index(slot_id)] = false;
    }
};

}  // namespace nds::client

TEST(StorageSlotTest, PacksQueueAndSlotIndexes) {
    const std::uint32_t slot_id = nds_storage_slot_id(7U, 11U);
    EXPECT_EQ(nds_storage_slot_id_queue(slot_id), 7U);
    EXPECT_EQ(nds_storage_slot_id_index(slot_id), 11U);
}

TEST(StorageSlotTest, AllocatesAcrossAvailableSlots) {
    nds::client::StorageClient client;
    nds::client::StorageClientTestAccess::mark_open(&client, 3U);
    const auto first = nds::client::StorageClientTestAccess::allocate(&client);
    ASSERT_TRUE(first.ok());
    const auto second = nds::client::StorageClientTestAccess::allocate(&client);
    ASSERT_TRUE(second.ok());
    const auto third = nds::client::StorageClientTestAccess::allocate(&client);
    ASSERT_TRUE(third.ok());
    EXPECT_FALSE(nds::client::StorageClientTestAccess::allocate(&client).ok());
    nds::client::StorageClientTestAccess::release_bookkeeping(&client, second.value());
    EXPECT_TRUE(nds::client::StorageClientTestAccess::allocate(&client).ok());
}
