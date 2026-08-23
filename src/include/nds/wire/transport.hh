#ifndef NDS_WIRE_TRANSPORT_HH
#define NDS_WIRE_TRANSPORT_HH

#include <stdint.h>

namespace nds::wire {

inline constexpr uint32_t kQpInfoMagic = UINT32_C(0x4e445331); // "NDS1"
inline constexpr uint16_t kQpInfoVersion = UINT16_C(3);
inline constexpr uint32_t kQpInfoBatchMagic = UINT32_C(0x4e445342); // "NDSB"
inline constexpr uint32_t kMaxQpInfoBatch = 1024U;
inline constexpr uint32_t kGidBytes = 16U;

struct __attribute__((packed)) QpInfoBatchHeader {
    uint32_t magic;
    uint16_t version;
    uint16_t reserved;
    uint32_t count;
};

static_assert(sizeof(QpInfoBatchHeader) == 12, "NDS RC QP batch header must remain fixed");

/*
 * Peer RC QP identity exchanged on TCP so each side can connect. Integer
 * fields are sent in network byte order. `gid` is a byte string.
 *
 * This is QP addressing, not a storage MR and not an HCCP/libibverbs object.
 */
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

}  // namespace nds::wire

namespace nds::transport {

/* Host-order peer QP identity used by the CPU verbs backend and the NPU RA adapter. */
struct QpInfo {
    uint32_t qp_num;
    uint32_t psn;
    uint16_t port_num;
    uint16_t gid_index;
    /* Reported MTU for diagnostics; not a cross-peer negotiation field. */
    uint32_t path_mtu;
    uint32_t traffic_class;
    uint32_t service_level;
    uint32_t retry_count;
    uint32_t retry_timeout;
    uint8_t gid[wire::kGidBytes];
};

enum class CodecResult {
    Ok,
    InvalidArgument,
    InvalidRecord,
};

CodecResult encode(const QpInfo *info, wire::QpInfo *encoded);
CodecResult decode(const wire::QpInfo *encoded, QpInfo *info);

/* Standard RC path-MTU byte values accepted by NDS's CPU verbs adapter. */
int mtu_is_supported(uint32_t mtu_bytes);

/*
 * Select the CPU RC QP's RTR path MTU from its local active port value. The
 * peer-reported value is diagnostic-only and does not affect the result.
 */
uint32_t select_mtu(uint32_t local_active_mtu, uint32_t peer_reported_mtu);

}  // namespace nds::transport

#endif
