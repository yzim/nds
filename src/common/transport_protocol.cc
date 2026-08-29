#include "transport_protocol.hh"

#include <arpa/inet.h>
#include <cstring>

namespace nds::transport {
namespace {

CodecResult validate_qp_info(const QpInfo *info) {
    if (info == nullptr) {
        return CodecResult::InvalidRecord;
    }
    if (info->qp_num == 0U || info->qp_num > UINT32_C(0x00ffffff)) {
        return CodecResult::InvalidRecord;
    }
    if (info->psn > UINT32_C(0x00ffffff)) {
        return CodecResult::InvalidRecord;
    }
    if (info->port_num == 0U) {
        return CodecResult::InvalidRecord;
    }
    if (info->path_mtu == 0U) {
        return CodecResult::InvalidRecord;
    }
    if (info->traffic_class > UINT8_MAX) {
        return CodecResult::InvalidRecord;
    }
    if (info->service_level > 15U) {
        return CodecResult::InvalidRecord;
    }
    if (info->retry_count > 7U || info->retry_timeout > 31U) {
        return CodecResult::InvalidRecord;
    }
    return CodecResult::Ok;
}

uint64_t host_to_network_u64(uint64_t value) {
    return (static_cast<uint64_t>(htonl(static_cast<uint32_t>(value))) << 32U) |
           htonl(static_cast<uint32_t>(value >> 32U));
}

uint64_t network_to_host_u64(uint64_t value) {
    return (static_cast<uint64_t>(ntohl(static_cast<uint32_t>(value))) << 32U) |
           ntohl(static_cast<uint32_t>(value >> 32U));
}

CodecResult validate_remote_memory(const RemoteMemory *memory) {
    if (memory == nullptr || memory->address == 0U || memory->length == 0U || memory->remote_key == 0U)
        return CodecResult::InvalidRecord;
    return CodecResult::Ok;
}

bool valid_transport_info_kind(TransportInfoKind kind) {
    return kind == TransportInfoKind::QpCountRequest || kind == TransportInfoKind::QpCountResponse ||
           kind == TransportInfoKind::QpEndpoints;
}

CodecResult validate_transport_info(const TransportInfo *info) {
    if (info == nullptr || !valid_transport_info_kind(info->kind) || info->qp_count == 0U ||
        info->qp_count > wire::kMaxQpInfoBatch) {
        return CodecResult::InvalidRecord;
    }
    if (info->kind != TransportInfoKind::QpEndpoints)
        return CodecResult::Ok;
    for (std::size_t index = 0U; index < info->qp_count; ++index) {
        if (validate_qp_info(&info->qps[index]) != CodecResult::Ok)
            return CodecResult::InvalidRecord;
    }
    return CodecResult::Ok;
}

}  // namespace

CodecResult encode(const QpInfo *info, wire::QpInfo *encoded) {
    if (encoded == nullptr) {
        return CodecResult::InvalidArgument;
    }
    if (validate_qp_info(info) != CodecResult::Ok) {
        return CodecResult::InvalidRecord;
    }

    *encoded = {};
    encoded->magic = htonl(wire::kQpInfoMagic);
    encoded->version = htons(wire::kQpInfoVersion);
    encoded->qp_num = htonl(info->qp_num);
    encoded->psn = htonl(info->psn);
    encoded->port_num = htons(info->port_num);
    encoded->gid_index = htons(info->gid_index);
    encoded->path_mtu = htonl(info->path_mtu);
    encoded->traffic_class = htonl(info->traffic_class);
    encoded->service_level = htonl(info->service_level);
    encoded->retry_count = htonl(info->retry_count);
    encoded->retry_timeout = htonl(info->retry_timeout);
    memcpy(encoded->gid, info->gid, sizeof(encoded->gid));
    return CodecResult::Ok;
}

CodecResult encode(const RemoteMemory *memory, wire::RemoteMemory *encoded) {
    if (encoded == nullptr)
        return CodecResult::InvalidArgument;
    if (validate_remote_memory(memory) != CodecResult::Ok)
        return CodecResult::InvalidRecord;
    *encoded = {};
    encoded->magic = htonl(wire::kRemoteMemoryMagic);
    encoded->version = htons(wire::kRemoteMemoryVersion);
    encoded->address = host_to_network_u64(memory->address);
    encoded->length = htonl(memory->length);
    encoded->remote_key = htonl(memory->remote_key);
    return CodecResult::Ok;
}

CodecResult encode(const TransportInfo *info, wire::TransportInfo *encoded) {
    if (encoded == nullptr)
        return CodecResult::InvalidArgument;
    if (validate_transport_info(info) != CodecResult::Ok)
        return CodecResult::InvalidRecord;
    *encoded = {};
    encoded->magic = htonl(wire::kTransportInfoMagic);
    encoded->version = htons(wire::kTransportInfoVersion);
    encoded->kind = htons(static_cast<uint16_t>(info->kind));
    encoded->qp_count = htonl(info->qp_count);
    if (info->kind != TransportInfoKind::QpEndpoints)
        return CodecResult::Ok;
    for (std::size_t index = 0U; index < info->qp_count; ++index) {
        if (encode(&info->qps[index], &encoded->qps[index]) != CodecResult::Ok)
            return CodecResult::InvalidRecord;
    }
    return CodecResult::Ok;
}

int mtu_is_supported(uint32_t mtu_bytes) {
    switch (mtu_bytes) {
        case 256U:
        case 512U:
        case 1024U:
        case 2048U:
        case 4096U:
            return 1;
        default:
            return 0;
    }
}

uint32_t select_mtu(uint32_t local_active_mtu, uint32_t peer_reported_mtu) {
    (void)peer_reported_mtu;
    return mtu_is_supported(local_active_mtu) ? local_active_mtu : 0U;
}

}  // namespace nds::transport

namespace nds::transport {

CodecResult decode(const wire::QpInfo *encoded, QpInfo *info) {
    QpInfo decoded;

    if (encoded == nullptr || info == nullptr) {
        return CodecResult::InvalidArgument;
    }
    if (ntohl(encoded->magic) != wire::kQpInfoMagic) {
        return CodecResult::InvalidRecord;
    }
    if (ntohs(encoded->version) != wire::kQpInfoVersion) {
        return CodecResult::InvalidRecord;
    }

    decoded = {};
    decoded.qp_num = ntohl(encoded->qp_num);
    decoded.psn = ntohl(encoded->psn);
    decoded.port_num = ntohs(encoded->port_num);
    decoded.gid_index = ntohs(encoded->gid_index);
    decoded.path_mtu = ntohl(encoded->path_mtu);
    decoded.traffic_class = ntohl(encoded->traffic_class);
    decoded.service_level = ntohl(encoded->service_level);
    decoded.retry_count = ntohl(encoded->retry_count);
    decoded.retry_timeout = ntohl(encoded->retry_timeout);
    memcpy(decoded.gid, encoded->gid, sizeof(decoded.gid));
    if (validate_qp_info(&decoded) != CodecResult::Ok) {
        return CodecResult::InvalidRecord;
    }
    *info = decoded;
    return CodecResult::Ok;
}

CodecResult decode(const wire::RemoteMemory *encoded, RemoteMemory *memory) {
    if (encoded == nullptr || memory == nullptr)
        return CodecResult::InvalidArgument;
    if (ntohl(encoded->magic) != wire::kRemoteMemoryMagic || ntohs(encoded->version) != wire::kRemoteMemoryVersion)
        return CodecResult::InvalidRecord;
    const RemoteMemory decoded{network_to_host_u64(encoded->address), ntohl(encoded->length),
                               ntohl(encoded->remote_key)};
    if (validate_remote_memory(&decoded) != CodecResult::Ok)
        return CodecResult::InvalidRecord;
    *memory = decoded;
    return CodecResult::Ok;
}

CodecResult decode(const wire::TransportInfo *encoded, TransportInfo *info) {
    if (encoded == nullptr || info == nullptr)
        return CodecResult::InvalidArgument;
    if (ntohl(encoded->magic) != wire::kTransportInfoMagic || ntohs(encoded->version) != wire::kTransportInfoVersion)
        return CodecResult::InvalidRecord;
    TransportInfo decoded{};
    decoded.kind = static_cast<TransportInfoKind>(ntohs(encoded->kind));
    decoded.qp_count = ntohl(encoded->qp_count);
    if (!valid_transport_info_kind(decoded.kind) || decoded.qp_count == 0U ||
        decoded.qp_count > wire::kMaxQpInfoBatch) {
        return CodecResult::InvalidRecord;
    }
    if (decoded.kind == TransportInfoKind::QpEndpoints) {
        for (std::size_t index = 0U; index < decoded.qp_count; ++index) {
            if (decode(&encoded->qps[index], &decoded.qps[index]) != CodecResult::Ok)
                return CodecResult::InvalidRecord;
        }
    }
    *info = decoded;
    return CodecResult::Ok;
}

}  // namespace nds::transport
