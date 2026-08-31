#ifndef NDS_TRANSPORT_PROTOCOL_HH
#define NDS_TRANSPORT_PROTOCOL_HH

#include <array>
#include <stdint.h>

namespace nds::wire {

inline constexpr uint32_t kQpInfoMagic = UINT32_C(0x4e445331);  // "NDS1"
inline constexpr uint16_t kQpInfoVersion = UINT16_C(3);
inline constexpr uint32_t kMaxQpInfoBatch = 8U;
inline constexpr uint32_t kTransportInfoMagic = UINT32_C(0x4e445354);  // "NDST"
inline constexpr uint16_t kTransportInfoVersion = UINT16_C(1);
inline constexpr uint32_t kRemoteMemoryMagic = UINT32_C(0x4e445332);  // "NDS2"
inline constexpr uint16_t kRemoteMemoryVersion = UINT16_C(1);
inline constexpr uint32_t kGidBytes = 16U;

/* Peer RC QP identity. All integers use network byte order. */
struct __attribute__((packed)) QpInfo {
    uint32_t magic;
    uint16_t version;
    uint16_t reserved0;
    uint32_t qp_num;
    uint32_t psn;
    uint16_t port_num;
    uint16_t gid_index;
    uint32_t path_mtu;
    uint32_t traffic_class;
    uint32_t service_level;
    uint32_t retry_count;
    uint32_t retry_timeout;
    uint8_t gid[kGidBytes];
    uint8_t reserved[24];
};

static_assert(sizeof(QpInfo) == 80, "NDS RC QP info must remain a fixed 80-byte message");

/* One fixed-size transport control record. Descriptor slots are meaningful
 * only for the endpoint-info message kind. */
struct __attribute__((packed)) TransportInfo {
    uint32_t magic;
    uint16_t version;
    uint16_t kind;
    uint32_t qp_count;
    QpInfo qps[kMaxQpInfoBatch];
};

static_assert(sizeof(TransportInfo) == 652, "NDS transport info must remain a fixed 652-byte message");

/* Remote memory metadata exchanged after QP setup. */
struct __attribute__((packed)) RemoteMemory {
    uint32_t magic;
    uint16_t version;
    uint16_t reserved0;
    uint64_t address;
    uint32_t length;
    uint32_t remote_key;
    uint8_t reserved[8];
};

static_assert(sizeof(RemoteMemory) == 32, "NDS remote-memory record must remain a fixed 32-byte message");

}  // namespace nds::wire

namespace nds {

/* Peer RC QP identity exchanged during the TCP bootstrap. */
struct QpInfo {
    uint32_t qp_num;
    uint32_t psn;
    uint16_t port_num;
    uint16_t gid_index;
    uint32_t path_mtu;
    uint32_t traffic_class;
    uint32_t service_level;
    uint32_t retry_count;
    uint32_t retry_timeout;
    uint8_t gid[wire::kGidBytes];
};

/* Peer MR identity exchanged after the QP bootstrap. */
struct RemoteMemory {
    uint64_t address;
    uint32_t length;
    uint32_t remote_key;
};

}  // namespace nds

namespace nds::transport {

enum class TransportInfoKind : uint16_t {
    QpCountRequest = 1U,
    QpCountResponse = 2U,
    QpEndpoints = 3U,
};

struct TransportInfo {
    TransportInfoKind kind{};
    uint32_t qp_count{};
    std::array<nds::QpInfo, wire::kMaxQpInfoBatch> qps{};
};

enum class CodecResult {
    Ok,
    InvalidArgument,
    InvalidRecord,
};

CodecResult encode(const nds::QpInfo *info, wire::QpInfo *encoded);
CodecResult decode(const wire::QpInfo *encoded, nds::QpInfo *info);
CodecResult encode(const nds::RemoteMemory *memory, wire::RemoteMemory *encoded);
CodecResult decode(const wire::RemoteMemory *encoded, nds::RemoteMemory *memory);
CodecResult encode(const TransportInfo *info, wire::TransportInfo *encoded);
CodecResult decode(const wire::TransportInfo *encoded, TransportInfo *info);

int mtu_is_supported(uint32_t mtu_bytes);
uint32_t select_mtu(uint32_t local_active_mtu, uint32_t peer_reported_mtu);

}  // namespace nds::transport

#endif
