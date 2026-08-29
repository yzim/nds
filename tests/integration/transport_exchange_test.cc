#include "tcp_socket.hh"
#include "transport_protocol.hh"

#include <arpa/inet.h>
#include <sys/socket.h>

#include <array>
#include <cstddef>
#include <future>

#include <gtest/gtest.h>

namespace {

nds::transport::QpInfo make_endpoint(std::uint32_t qpn, std::uint32_t psn) {
    nds::transport::QpInfo endpoint{};
    endpoint.qp_num = qpn;
    endpoint.psn = psn;
    endpoint.port_num = 1U;
    endpoint.gid_index = 3U;
    endpoint.path_mtu = 4096U;
    endpoint.retry_count = 7U;
    endpoint.retry_timeout = 14U;
    endpoint.gid[0] = 0xfeU;
    endpoint.gid[1] = 0x80U;
    return endpoint;
}

nds::transport::TransportInfo endpoint_info(std::uint32_t count, nds::transport::QpInfo first,
                                            nds::transport::QpInfo second = {}) {
    nds::transport::TransportInfo info{nds::transport::TransportInfoKind::QpEndpoints, count, {}};
    info.qps[0] = first;
    if (count > 1U)
        info.qps[1] = second;
    return info;
}

nds::Result<void> send_info(nds::TcpConnection *channel, const nds::transport::TransportInfo &info) {
    nds::wire::TransportInfo encoded{};
    if (nds::transport::encode(&info, &encoded) != nds::transport::CodecResult::Ok)
        return nds::unexpected(nds::ErrorCode::kTransport, "cannot encode transport information");
    return channel->send(std::as_bytes(std::span{&encoded, 1U}));
}

nds::Result<nds::transport::TransportInfo> receive_info(nds::TcpConnection *channel) {
    nds::wire::TransportInfo encoded{};
    if (const auto received = channel->receive(std::as_writable_bytes(std::span{&encoded, 1U})); !received)
        return nds::unexpected(received.error());
    nds::transport::TransportInfo info{};
    if (nds::transport::decode(&encoded, &info) != nds::transport::CodecResult::Ok)
        return nds::unexpected(nds::ErrorCode::kTransport, "invalid transport information");
    return info;
}

}  // namespace

TEST(TransportExchangeIntegrationTest, NegotiatesQpCountThenExchangesEndpointInfo) {
    int sockets[2]{};
    ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets), 0);
    const auto client = endpoint_info(2U, make_endpoint(0x1101U, 0x2202U), make_endpoint(0x1102U, 0x2203U));
    const auto server = endpoint_info(2U, make_endpoint(0x3303U, 0x4404U), make_endpoint(0x3304U, 0x4405U));
    std::future<nds::Result<nds::transport::TransportInfo>> server_result =
        std::async(std::launch::async, [fd = sockets[1], server]() -> nds::Result<nds::transport::TransportInfo> {
            nds::TcpConnection channel{fd};
            const auto request = receive_info(&channel);
            if (!request || request->kind != nds::transport::TransportInfoKind::QpCountRequest)
                return nds::unexpected(nds::ErrorCode::kTransport, "expected QP-count request");
            const nds::transport::TransportInfo response{nds::transport::TransportInfoKind::QpCountResponse, 2U, {}};
            if (const auto sent = send_info(&channel, response); !sent)
                return nds::unexpected(sent.error());
            const auto peer = receive_info(&channel);
            if (!peer)
                return nds::unexpected(peer.error());
            if (const auto sent = send_info(&channel, server); !sent)
                return nds::unexpected(sent.error());
            return peer;
        });
    nds::TcpConnection channel{sockets[0]};
    const nds::transport::TransportInfo request{nds::transport::TransportInfoKind::QpCountRequest, 6U, {}};
    ASSERT_TRUE(send_info(&channel, request));
    const auto response = receive_info(&channel);
    ASSERT_TRUE(response) << response.error().message;
    ASSERT_EQ(response->kind, nds::transport::TransportInfoKind::QpCountResponse);
    ASSERT_EQ(response->qp_count, 2U);
    ASSERT_TRUE(send_info(&channel, client));
    const auto peer = receive_info(&channel);
    const auto remote = server_result.get();

    ASSERT_TRUE(peer) << peer.error().message;
    ASSERT_TRUE(remote) << remote.error().message;
    EXPECT_EQ(peer->qps[0].qp_num, server.qps[0].qp_num);
    EXPECT_EQ(peer->qps[1].qp_num, server.qps[1].qp_num);
    EXPECT_EQ(remote->qps[0].qp_num, client.qps[0].qp_num);
    EXPECT_EQ(remote->qps[1].qp_num, client.qps[1].qp_num);
}

TEST(TransportExchangeIntegrationTest, RejectsEndpointInfoWithInvalidQpCount) {
    nds::transport::TransportInfo info{
        nds::transport::TransportInfoKind::QpEndpoints, nds::wire::kMaxQpInfoBatch + 1U, {}};
    nds::wire::TransportInfo encoded{};
    EXPECT_EQ(nds::transport::encode(&info, &encoded), nds::transport::CodecResult::InvalidRecord);
}

TEST(TransportExchangeIntegrationTest, RejectsInvalidTransportInfoVersion) {
    nds::wire::TransportInfo encoded{};
    encoded.magic = htonl(nds::wire::kTransportInfoMagic);
    encoded.version = htons(nds::wire::kTransportInfoVersion + 1U);
    encoded.kind = htons(static_cast<std::uint16_t>(nds::transport::TransportInfoKind::QpCountRequest));
    encoded.qp_count = htonl(1U);
    nds::transport::TransportInfo decoded{};
    EXPECT_EQ(nds::transport::decode(&encoded, &decoded), nds::transport::CodecResult::InvalidRecord);
}

TEST(TransportExchangeIntegrationTest, TransfersExactRawBytesOverSocketPair) {
    int sockets[2]{};
    ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets), 0);
    const std::array<std::byte, 3U> sent{std::byte{0x11}, std::byte{0x22}, std::byte{0x33}};
    std::array<std::byte, sent.size()> received{};
    std::future<nds::Result<void>> receiver = std::async(std::launch::async, [fd = sockets[1], &received] {
        nds::TcpConnection channel{fd};
        return channel.receive(received);
    });
    nds::TcpConnection channel{sockets[0]};
    ASSERT_TRUE(channel.send(sent));
    ASSERT_TRUE(receiver.get());
    EXPECT_EQ(received, sent);
}
