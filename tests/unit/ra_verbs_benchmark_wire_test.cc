#include "rdma_benchmark_wire.hh"

#include <array>

#include <gtest/gtest.h>

TEST(RaVerbsBenchmarkWireTest, RoundTripsRemoteMemory) {
    const nds::benchmark::RemoteMemory source{nds::benchmark::Operation::Read, 0x12340000U, 4096U, 0xabcdefU};
    std::array<std::uint8_t, nds::benchmark::kMemoryRecordBytes> bytes{};
    nds::benchmark::RemoteMemory decoded{};

    ASSERT_TRUE(nds::benchmark::serialize_remote_memory(source, &bytes));
    ASSERT_TRUE(nds::benchmark::deserialize_remote_memory(bytes, &decoded));
    EXPECT_EQ(decoded.operation, source.operation);
    EXPECT_EQ(decoded.address, source.address);
    EXPECT_EQ(decoded.length, source.length);
    EXPECT_EQ(decoded.remote_key, source.remote_key);
}

TEST(RaVerbsBenchmarkWireTest, RejectsInvalidRecords) {
    std::array<std::uint8_t, nds::benchmark::kMemoryRecordBytes> bytes{};
    nds::benchmark::RemoteMemory decoded{};

    EXPECT_FALSE(nds::benchmark::deserialize_remote_memory(bytes, &decoded));
    EXPECT_FALSE(nds::benchmark::serialize_remote_memory(
        {nds::benchmark::Operation::Write, 0U, 4096U, 0xabcdefU}, &bytes));
}
