#include "storage_protocol.hh"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>

namespace {

constexpr std::uint64_t kCommandId = UINT64_C(0x1020304050607080);
constexpr std::uint64_t kAddress = UINT64_C(0x1122334455667788);
constexpr std::uint32_t kRemoteKey = UINT32_C(0xa1b2c3d4);

template <typename T>
void expect_memory(const T &value, std::uint64_t address, std::uint64_t length, std::uint32_t remote_key) {
    EXPECT_EQ(value.address, address);
    EXPECT_EQ(value.length, length);
    EXPECT_EQ(value.remote_key, remote_key);
}

std::array<std::uint8_t, nds::kStorageCommandBytes> read_golden() {
    return {0x4eU, 0x44U, 0x53U, 0x43U, 0x00U, 0x03U, 0x00U, 0x01U, 0x10U, 0x20U, 0x30U, 0x40U, 0x50U, 0x60U,
            0x70U, 0x80U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x10U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
            0x00U, 0x00U, 0x20U, 0x00U, 0x11U, 0x22U, 0x33U, 0x44U, 0x55U, 0x66U, 0x77U, 0x88U, 0x00U, 0x00U,
            0x00U, 0x00U, 0x00U, 0x00U, 0x20U, 0x00U, 0xa1U, 0xb2U, 0xc3U, 0xd4U, 0x00U, 0x00U, 0x00U, 0x02U};
}

std::array<std::uint8_t, nds::kStorageCommandBytes> write_golden() {
    auto bytes = read_golden();
    bytes[7] = 0x02U;
    bytes[55] = 0x04U;
    return bytes;
}

std::array<std::uint8_t, nds::kStorageCommandBytes> batch_read_golden() {
    auto bytes = read_golden();
    bytes[7] = 0x03U;
    for (std::size_t index = 16U; index < 24U; ++index) bytes[index] = 0U;
    bytes[23] = 0x02U;
    for (std::size_t index = 24U; index < 32U; ++index) bytes[index] = 0U;
    bytes[30] = 0x30U;
    bytes[31] = 0x00U;
    for (std::size_t index = 40U; index < 48U; ++index) bytes[index] = 0U;
    bytes[47] = 0x50U;
    bytes[55] = 0x04U;
    return bytes;
}

std::array<std::uint8_t, nds::kStorageCommandBytes> batch_write_golden() {
    auto bytes = batch_read_golden();
    bytes[7] = 0x04U;
    return bytes;
}

}  // namespace

TEST(StorageProtocolTest, RoundTripsReadAndWrite) {
    std::array<std::uint8_t, nds::kStorageCommandBytes> bytes{};
    const nds::StorageReadCommand read{kCommandId, 4096U, 8192U, {kAddress, 8192U, kRemoteKey}};
    nds::StorageReadCommand decoded_read{};
    ASSERT_EQ(nds::serialize_storage_read(read, bytes.data(), bytes.size()), nds::StorageSerdeResult::Ok);
    EXPECT_EQ(bytes, read_golden());
    ASSERT_EQ(nds::deserialize_storage_read(bytes.data(), bytes.size(), &decoded_read), nds::StorageSerdeResult::Ok);
    EXPECT_EQ(decoded_read.command_id, read.command_id);
    EXPECT_EQ(decoded_read.offset, read.offset);
    EXPECT_EQ(decoded_read.length, read.length);
    expect_memory(decoded_read.data, read.data.address, read.data.length, read.data.remote_key);

    const nds::StorageWriteCommand write{kCommandId, 4096U, 8192U, {kAddress, 8192U, kRemoteKey}};
    nds::StorageWriteCommand decoded_write{};
    ASSERT_EQ(nds::serialize_storage_write(write, bytes.data(), bytes.size()), nds::StorageSerdeResult::Ok);
    EXPECT_EQ(bytes, write_golden());
    ASSERT_EQ(nds::deserialize_storage_write(bytes.data(), bytes.size(), &decoded_write), nds::StorageSerdeResult::Ok);
    EXPECT_EQ(decoded_write.command_id, write.command_id);
    EXPECT_EQ(decoded_write.offset, write.offset);
    EXPECT_EQ(decoded_write.length, write.length);
    expect_memory(decoded_write.data, write.data.address, write.data.length, write.data.remote_key);
}

TEST(StorageProtocolTest, RoundTripsCommandSlotIndex) {
    std::array<std::uint8_t, nds::kStorageCommandBytes> bytes{};
    const nds::StorageReadCommand read{
        .command_id = kCommandId,
        .offset = 0U,
        .length = 64U,
        .data = {kAddress, 64U, kRemoteKey},
        .slot_index = 3U,
    };
    nds::StorageReadCommand decoded{};

    ASSERT_EQ(nds::serialize_storage_read(read, bytes.data(), bytes.size()), nds::StorageSerdeResult::Ok);
    EXPECT_EQ(bytes[56U], 0x00U);
    EXPECT_EQ(bytes[57U], 0x00U);
    EXPECT_EQ(bytes[58U], 0x00U);
    EXPECT_EQ(bytes[59U], 0x03U);
    ASSERT_EQ(nds::deserialize_storage_read(bytes.data(), bytes.size(), &decoded), nds::StorageSerdeResult::Ok);
    EXPECT_EQ(decoded.slot_index, read.slot_index);
}

TEST(StorageProtocolTest, RoundTripsBatchCommandsAndEntries) {
    std::array<std::uint8_t, nds::kStorageCommandBytes> command_bytes{};
    std::array<std::uint8_t, nds::kStorageBatchEntryBytes> entry_bytes{};
    const nds::StorageMemory entries{kAddress, 2U * nds::kStorageBatchEntryBytes, kRemoteKey};

    const nds::StorageBatchReadCommand read{kCommandId, 2U, 12288U, entries};
    nds::StorageBatchReadCommand decoded_read{};
    ASSERT_EQ(nds::serialize_storage_batch_read(read, command_bytes.data(), command_bytes.size()),
              nds::StorageSerdeResult::Ok);
    EXPECT_EQ(command_bytes, batch_read_golden());
    ASSERT_EQ(nds::deserialize_storage_batch_read(command_bytes.data(), command_bytes.size(), &decoded_read),
              nds::StorageSerdeResult::Ok);
    EXPECT_EQ(decoded_read.entry_count, read.entry_count);
    EXPECT_EQ(decoded_read.total_length, read.total_length);
    expect_memory(decoded_read.entries, entries.address, entries.length, entries.remote_key);

    const nds::StorageBatchWriteCommand write{kCommandId, 2U, 12288U, entries};
    nds::StorageBatchWriteCommand decoded_write{};
    ASSERT_EQ(nds::serialize_storage_batch_write(write, command_bytes.data(), command_bytes.size()),
              nds::StorageSerdeResult::Ok);
    EXPECT_EQ(command_bytes, batch_write_golden());
    ASSERT_EQ(nds::deserialize_storage_batch_write(command_bytes.data(), command_bytes.size(), &decoded_write),
              nds::StorageSerdeResult::Ok);
    EXPECT_EQ(decoded_write.entry_count, write.entry_count);
    EXPECT_EQ(decoded_write.total_length, write.total_length);

    const nds::StorageBatchReadEntry read_entry{4096U, 8192U, {kAddress, 8192U, kRemoteKey}};
    nds::StorageBatchReadEntry decoded_read_entry{};
    ASSERT_EQ(nds::serialize_storage_batch_read_entry(read_entry, entry_bytes.data(), entry_bytes.size()),
              nds::StorageSerdeResult::Ok);
    EXPECT_EQ(entry_bytes[39], 0x02U);
    ASSERT_EQ(nds::deserialize_storage_batch_read_entry(entry_bytes.data(), entry_bytes.size(), &decoded_read_entry),
              nds::StorageSerdeResult::Ok);
    EXPECT_EQ(decoded_read_entry.offset, read_entry.offset);
    EXPECT_EQ(decoded_read_entry.length, read_entry.length);

    const nds::StorageBatchWriteEntry write_entry{4096U, 8192U, {kAddress, 8192U, kRemoteKey}};
    nds::StorageBatchWriteEntry decoded_write_entry{};
    ASSERT_EQ(nds::serialize_storage_batch_write_entry(write_entry, entry_bytes.data(), entry_bytes.size()),
              nds::StorageSerdeResult::Ok);
    EXPECT_EQ(entry_bytes[39], 0x04U);
    ASSERT_EQ(nds::deserialize_storage_batch_write_entry(entry_bytes.data(), entry_bytes.size(), &decoded_write_entry),
              nds::StorageSerdeResult::Ok);
    EXPECT_EQ(decoded_write_entry.offset, write_entry.offset);
    EXPECT_EQ(decoded_write_entry.length, write_entry.length);
}

TEST(StorageProtocolTest, RoundTripsSessionAndCompletionRecords) {
    std::array<std::uint8_t, nds::kStorageBootstrapBytes> bootstrap_bytes{};
    std::array<std::uint8_t, nds::kStorageNamespaceBytes> namespace_bytes{};
    std::array<std::uint8_t, nds::kStorageCompletionBytes> completion_bytes{};

    const nds::StorageBootstrap bootstrap{
        .completion = {kAddress, nds::kStorageCompletionBytes, kRemoteKey},
        .namespace_response = {kAddress + 4096U, nds::kStorageNamespaceBytes, kRemoteKey},
        .slots = {kAddress + 8192U, 2U * sizeof(nds::StorageSlot), kRemoteKey},
        .slot_count = 2U,
    };
    nds::StorageBootstrap decoded_bootstrap{};
    ASSERT_EQ(nds::serialize_storage_bootstrap(bootstrap, bootstrap_bytes.data(), bootstrap_bytes.size()),
              nds::StorageSerdeResult::Ok);
    ASSERT_EQ(nds::deserialize_storage_bootstrap(bootstrap_bytes.data(), bootstrap_bytes.size(), &decoded_bootstrap),
              nds::StorageSerdeResult::Ok);
    expect_memory(decoded_bootstrap.completion, kAddress, nds::kStorageCompletionBytes, kRemoteKey);
    expect_memory(decoded_bootstrap.namespace_response, kAddress + 4096U, nds::kStorageNamespaceBytes, kRemoteKey);
    expect_memory(decoded_bootstrap.slots, kAddress + 8192U, 2U * sizeof(nds::StorageSlot), kRemoteKey);
    EXPECT_EQ(decoded_bootstrap.slot_count, bootstrap.slot_count);

    const nds::StorageNamespace storage_namespace{UINT64_C(1024) * 1024U};
    nds::StorageNamespace decoded_namespace{};
    ASSERT_EQ(nds::serialize_storage_namespace(storage_namespace, namespace_bytes.data(), namespace_bytes.size()),
              nds::StorageSerdeResult::Ok);
    ASSERT_EQ(nds::deserialize_storage_namespace(namespace_bytes.data(), namespace_bytes.size(), &decoded_namespace),
              nds::StorageSerdeResult::Ok);
    EXPECT_EQ(decoded_namespace.capacity, storage_namespace.capacity);

    const nds::StorageCompletion completion{kCommandId, nds::StorageCompletionState::Complete,
                                            nds::StorageStatus::Success, 8192U};
    nds::StorageCompletion decoded_completion{};
    ASSERT_EQ(nds::serialize_storage_completion(completion, completion_bytes.data(), completion_bytes.size()),
              nds::StorageSerdeResult::Ok);
    EXPECT_EQ(completion_bytes[0], 0x4eU);
    EXPECT_EQ(completion_bytes[3], 0x44U);
    ASSERT_EQ(
        nds::deserialize_storage_completion(completion_bytes.data(), completion_bytes.size(), &decoded_completion),
        nds::StorageSerdeResult::Ok);
    EXPECT_EQ(decoded_completion.command_id, completion.command_id);
    EXPECT_EQ(decoded_completion.state, completion.state);
    EXPECT_EQ(decoded_completion.status, completion.status);
    EXPECT_EQ(decoded_completion.bytes_transferred, completion.bytes_transferred);
}

TEST(StorageProtocolTest, RejectsWrongOperationAndAccess) {
    std::array<std::uint8_t, nds::kStorageCommandBytes> bytes{};
    const nds::StorageReadCommand read{kCommandId, 0U, 64U, {kAddress, 64U, kRemoteKey}};
    nds::StorageReadCommand decoded{};
    ASSERT_EQ(nds::serialize_storage_read(read, bytes.data(), bytes.size()), nds::StorageSerdeResult::Ok);

    bytes[7] = static_cast<std::uint8_t>(nds::StorageOperation::Write);
    EXPECT_EQ(nds::deserialize_storage_read(bytes.data(), bytes.size(), &decoded),
              nds::StorageSerdeResult::InvalidRecord);
    bytes[7] = static_cast<std::uint8_t>(nds::StorageOperation::Read);
    bytes[55] = 0x04U;
    EXPECT_EQ(nds::deserialize_storage_read(bytes.data(), bytes.size(), &decoded),
              nds::StorageSerdeResult::InvalidRecord);
}

TEST(StorageProtocolTest, RejectsIncompleteBootstrapResponseDescriptor) {
    std::array<std::uint8_t, nds::kStorageBootstrapBytes> bytes{};
    const nds::StorageBootstrap bootstrap{
        .completion = {kAddress, nds::kStorageCompletionBytes, kRemoteKey},
        .namespace_response = {kAddress + 4096U, nds::kStorageNamespaceBytes, kRemoteKey},
        .slots = {kAddress + 8192U, sizeof(nds::StorageSlot), kRemoteKey},
        .slot_count = 1U,
    };
    nds::StorageBootstrap decoded{};
    ASSERT_EQ(nds::serialize_storage_bootstrap(bootstrap, bytes.data(), bytes.size()), nds::StorageSerdeResult::Ok);

    // Access fields use the big-endian wire encoding; clear the low byte.
    bytes[55U] = 0U;
    EXPECT_EQ(nds::deserialize_storage_bootstrap(bytes.data(), bytes.size(), &decoded),
              nds::StorageSerdeResult::InvalidRecord);

    ASSERT_EQ(nds::serialize_storage_bootstrap(bootstrap, bytes.data(), bytes.size()), nds::StorageSerdeResult::Ok);
    bytes[5U] = 2U;
    EXPECT_EQ(nds::deserialize_storage_bootstrap(bytes.data(), bytes.size(), &decoded),
              nds::StorageSerdeResult::InvalidRecord);
}
