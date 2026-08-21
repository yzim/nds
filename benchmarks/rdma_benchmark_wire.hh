#ifndef NDS_RDMA_BENCHMARK_WIRE_HH
#define NDS_RDMA_BENCHMARK_WIRE_HH

#include <array>
#include <cstddef>
#include <cstdint>

namespace nds::benchmark {

inline constexpr std::uint32_t kMemoryRecordBytes = 32U;
inline constexpr std::uint32_t kMemoryRecordMagic = 0x4e445352U;  // "NDSR"
inline constexpr std::uint16_t kMemoryRecordVersion = 1U;

enum class Operation : std::uint16_t {
    Read = 1U,
    Write = 2U,
};

struct RemoteMemory {
    Operation operation;
    std::uint64_t address;
    std::uint64_t length;
    std::uint32_t remote_key;
};

inline void write_u16(std::uint8_t *bytes, std::uint16_t value) {
    bytes[0] = static_cast<std::uint8_t>(value >> 8U);
    bytes[1] = static_cast<std::uint8_t>(value);
}

inline void write_u32(std::uint8_t *bytes, std::uint32_t value) {
    bytes[0] = static_cast<std::uint8_t>(value >> 24U);
    bytes[1] = static_cast<std::uint8_t>(value >> 16U);
    bytes[2] = static_cast<std::uint8_t>(value >> 8U);
    bytes[3] = static_cast<std::uint8_t>(value);
}

inline void write_u64(std::uint8_t *bytes, std::uint64_t value) {
    write_u32(bytes, static_cast<std::uint32_t>(value >> 32U));
    write_u32(bytes + 4U, static_cast<std::uint32_t>(value));
}

inline std::uint16_t read_u16(const std::uint8_t *bytes) {
    return static_cast<std::uint16_t>((static_cast<std::uint16_t>(bytes[0]) << 8U) | bytes[1]);
}

inline std::uint32_t read_u32(const std::uint8_t *bytes) {
    return (static_cast<std::uint32_t>(bytes[0]) << 24U) | (static_cast<std::uint32_t>(bytes[1]) << 16U) |
           (static_cast<std::uint32_t>(bytes[2]) << 8U) | bytes[3];
}

inline std::uint64_t read_u64(const std::uint8_t *bytes) {
    return (static_cast<std::uint64_t>(read_u32(bytes)) << 32U) | read_u32(bytes + 4U);
}

inline bool serialize_remote_memory(const RemoteMemory &memory,
                                    std::array<std::uint8_t, kMemoryRecordBytes> *bytes) {
    if (bytes == nullptr || memory.address == 0U || memory.length == 0U || memory.remote_key == 0U)
        return false;
    bytes->fill(0U);
    write_u32(bytes->data(), kMemoryRecordMagic);
    write_u16(bytes->data() + 4U, kMemoryRecordVersion);
    write_u16(bytes->data() + 6U, static_cast<std::uint16_t>(memory.operation));
    write_u64(bytes->data() + 8U, memory.address);
    write_u64(bytes->data() + 16U, memory.length);
    write_u32(bytes->data() + 24U, memory.remote_key);
    return true;
}

inline bool deserialize_remote_memory(const std::array<std::uint8_t, kMemoryRecordBytes> &bytes,
                                      RemoteMemory *memory) {
    if (memory == nullptr || read_u32(bytes.data()) != kMemoryRecordMagic ||
        read_u16(bytes.data() + 4U) != kMemoryRecordVersion)
        return false;
    const std::uint16_t operation = read_u16(bytes.data() + 6U);
    if (operation != static_cast<std::uint16_t>(Operation::Read) && operation != static_cast<std::uint16_t>(Operation::Write))
        return false;
    *memory = {static_cast<Operation>(operation), read_u64(bytes.data() + 8U), read_u64(bytes.data() + 16U),
               read_u32(bytes.data() + 24U)};
    return memory->address != 0U && memory->length != 0U && memory->remote_key != 0U;
}

inline const char *operation_name(Operation operation) {
    return operation == Operation::Read ? "read" : "write";
}

}  // namespace nds::benchmark

#endif
