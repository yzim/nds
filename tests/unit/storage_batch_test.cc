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
