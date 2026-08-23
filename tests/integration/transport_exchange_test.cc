#include "nds/tcp_bootstrap.hh"

#include <sys/socket.h>
#include <unistd.h>

#include <cstring>
#include <future>
#include <vector>

#include <gtest/gtest.h>

namespace {

nds::transport::QpInfo make_endpoint(std::uint32_t qpn, std::uint32_t psn) {
    nds::transport::QpInfo endpoint{};
    endpoint.qp_num = qpn;
    endpoint.psn = psn;
    endpoint.port_num = 1;
    endpoint.gid_index = 3;
    endpoint.path_mtu = 4096;
    endpoint.traffic_class = 0;
    endpoint.service_level = 0;
    endpoint.retry_count = 7;
    endpoint.retry_timeout = 14;
    endpoint.gid[0] = 0xfe;
    endpoint.gid[1] = 0x80;
    return endpoint;
}

}  // namespace

TEST(TransportExchangeIntegrationTest, ExchangesEndpointRecordsOverSocketPair) {
    int sockets[2]{};
    ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets), 0);
    const auto client = make_endpoint(0x1101, 0x2202);
    const auto server = make_endpoint(0x3303, 0x4404);
    std::future<nds::Result<nds::transport::QpInfo>> server_result = std::async(
        std::launch::async, [fd = sockets[1], server] { return nds::TcpPeerExchange{fd}.exchange_as_server(server); });
    const auto client_result = nds::TcpPeerExchange{sockets[0]}.exchange_as_client(client);
    const auto remote_result = server_result.get();

    ASSERT_TRUE(client_result) << client_result.error().message;
    ASSERT_TRUE(remote_result) << remote_result.error().message;
    EXPECT_EQ(client_result->qp_num, server.qp_num);
    EXPECT_EQ(remote_result->qp_num, client.qp_num);
    EXPECT_EQ(client_result->psn, server.psn);
    EXPECT_EQ(remote_result->psn, client.psn);
    close(sockets[0]);
    close(sockets[1]);
}

TEST(TransportExchangeIntegrationTest, ExchangesQpBatchOverOneSocket) {
    int sockets[2]{};
    ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets), 0);
    const std::vector<nds::transport::QpInfo> client{
        make_endpoint(0x1101, 0x2202), make_endpoint(0x1102, 0x2203)};
    const std::vector<nds::transport::QpInfo> server{
        make_endpoint(0x3303, 0x4404), make_endpoint(0x3304, 0x4405)};
    std::future<nds::Result<std::vector<nds::transport::QpInfo>>> server_result = std::async(
        std::launch::async, [fd = sockets[1], server] { return nds::TcpPeerExchange{fd}.exchange_as_server(server); });
    const auto client_result = nds::TcpPeerExchange{sockets[0]}.exchange_as_client(client);
    const auto remote_result = server_result.get();

    ASSERT_TRUE(client_result) << client_result.error().message;
    ASSERT_TRUE(remote_result) << remote_result.error().message;
    ASSERT_EQ(client_result->size(), server.size());
    ASSERT_EQ(remote_result->size(), client.size());
    EXPECT_EQ((*client_result)[0].qp_num, server[0].qp_num);
    EXPECT_EQ((*client_result)[1].qp_num, server[1].qp_num);
    EXPECT_EQ((*remote_result)[0].qp_num, client[0].qp_num);
    EXPECT_EQ((*remote_result)[1].qp_num, client[1].qp_num);
    close(sockets[0]);
    close(sockets[1]);
}
