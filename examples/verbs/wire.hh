#ifndef NDS_EXAMPLES_VERBS_WIRE_HH
#define NDS_EXAMPLES_VERBS_WIRE_HH

#include "result.hh"
#include "tcp_socket.hh"
#include "transport_protocol.hh"

#include <cstdint>
#include <span>

namespace nds::examples::verbs {

inline Result<transport::QpInfo> exchange_client_qp(TcpConnection &channel, const transport::QpInfo &local) {
    wire::QpInfo local_record{};
    wire::QpInfo peer_record{};
    if (transport::encode(&local, &local_record) != transport::CodecResult::Ok)
        return Error{ErrorCode::kTransport, "cannot encode QP information"};
    if (const auto sent = channel.send(std::as_bytes(std::span{&local_record, 1U})); !sent)
        return Error{sent.error()};
    if (const auto received = channel.receive(std::as_writable_bytes(std::span{&peer_record, 1U})); !received)
        return Error{received.error()};
    transport::QpInfo peer{};
    if (transport::decode(&peer_record, &peer) != transport::CodecResult::Ok)
        return Error{ErrorCode::kTransport, "peer sent invalid QP information"};
    return peer;
}

inline Result<transport::QpInfo> exchange_server_qp(TcpConnection &channel, const transport::QpInfo &local) {
    wire::QpInfo local_record{};
    wire::QpInfo peer_record{};
    if (const auto received = channel.receive(std::as_writable_bytes(std::span{&peer_record, 1U})); !received)
        return Error{received.error()};
    if (transport::encode(&local, &local_record) != transport::CodecResult::Ok)
        return Error{ErrorCode::kTransport, "cannot encode QP information"};
    if (const auto sent = channel.send(std::as_bytes(std::span{&local_record, 1U})); !sent)
        return Error{sent.error()};
    transport::QpInfo peer{};
    if (transport::decode(&peer_record, &peer) != transport::CodecResult::Ok)
        return Error{ErrorCode::kTransport, "peer sent invalid QP information"};
    return peer;
}

inline Result<void> send_ready(TcpConnection &channel) {
    const std::uint8_t ready{1U};
    return channel.send(std::as_bytes(std::span{&ready, 1U}));
}

inline Result<void> wait_ready(TcpConnection &channel) {
    std::uint8_t ready{};
    if (const auto received = channel.receive(std::as_writable_bytes(std::span{&ready, 1U})); !received)
        return Error{received.error()};
    return ready == 1U ? Result<void>{} : Error{ErrorCode::kTransport, "peer sent an invalid readiness record"};
}

}  // namespace nds::examples::verbs

#endif
