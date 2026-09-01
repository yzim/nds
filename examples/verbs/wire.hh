#ifndef NDS_EXAMPLES_VERBS_WIRE_HH
#define NDS_EXAMPLES_VERBS_WIRE_HH

#include "result.hh"
#include "tcp_socket.hh"
#include "transport_protocol.hh"

#include <cstdint>
#include <span>

namespace nds::examples::verbs {

inline Result<QpInfo> exchange_client_qp(TcpConnection &channel, const QpInfo &local) {
    wire::QpInfo local_record{};
    wire::QpInfo peer_record{};
    if (transport::encode(&local, &local_record) != transport::CodecResult::Ok)
        return Error{ErrorCode::kTransport, "cannot encode QP information"};
    if (const auto sent = channel.send(std::as_bytes(std::span{&local_record, 1U})); !sent.ok())
        return Error{sent.error()};
    if (const auto received = channel.receive(std::as_writable_bytes(std::span{&peer_record, 1U})); !received.ok())
        return Error{received.error()};
    QpInfo peer{};
    if (transport::decode(&peer_record, &peer) != transport::CodecResult::Ok)
        return Error{ErrorCode::kTransport, "peer sent invalid QP information"};
    return peer;
}

inline Result<QpInfo> exchange_server_qp(TcpConnection &channel, const QpInfo &local) {
    wire::QpInfo local_record{};
    wire::QpInfo peer_record{};
    if (const auto received = channel.receive(std::as_writable_bytes(std::span{&peer_record, 1U})); !received.ok())
        return Error{received.error()};
    if (transport::encode(&local, &local_record) != transport::CodecResult::Ok)
        return Error{ErrorCode::kTransport, "cannot encode QP information"};
    if (const auto sent = channel.send(std::as_bytes(std::span{&local_record, 1U})); !sent.ok())
        return Error{sent.error()};
    QpInfo peer{};
    if (transport::decode(&peer_record, &peer) != transport::CodecResult::Ok)
        return Error{ErrorCode::kTransport, "peer sent invalid QP information"};
    return peer;
}

inline Result<void> send_barrier(TcpConnection &channel) {
    const std::uint8_t ready{1U};
    return channel.send(std::as_bytes(std::span{&ready, 1U}));
}

inline Result<void> wait_barrier(TcpConnection &channel) {
    std::uint8_t ready{};
    if (const auto received = channel.receive(std::as_writable_bytes(std::span{&ready, 1U})); !received.ok())
        return Error{received.error()};
    return ready == 1U ? Result<void>{} : Error{ErrorCode::kTransport, "peer sent an invalid barrier record"};
}

inline Result<RemoteMemory> receive_remote_memory(TcpConnection &channel) {
    wire::RemoteMemory record{};
    if (const auto received = channel.receive(std::as_writable_bytes(std::span{&record, 1U})); !received.ok())
        return Error{received.error()};
    RemoteMemory memory{};
    if (transport::decode(&record, &memory) != transport::CodecResult::Ok)
        return Error{ErrorCode::kTransport, "peer sent invalid remote-memory information"};
    return memory;
}

inline Result<void> send_remote_memory(TcpConnection &channel, const RemoteMemory &memory) {
    wire::RemoteMemory record{};
    if (transport::encode(&memory, &record) != transport::CodecResult::Ok)
        return Error{ErrorCode::kTransport, "cannot encode remote-memory information"};
    return channel.send(std::as_bytes(std::span{&record, 1U}));
}

}  // namespace nds::examples::verbs

#endif
