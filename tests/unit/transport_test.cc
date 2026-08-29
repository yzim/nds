#include "transport_protocol.hh"
#include "tcp_socket.hh"

#include <arpa/inet.h>

#include <cstring>

#include <gtest/gtest.h>

namespace {

nds::transport::QpInfo endpoint() {
    nds::transport::QpInfo value{};
    value.qp_num = 0x00abcdU;
    value.psn = 0x00123456U;
    value.port_num = 1U;
    value.gid_index = 3U;
    value.path_mtu = 4096U;
    value.traffic_class = 106U;
    value.service_level = 5U;
    value.retry_count = 7U;
    value.retry_timeout = 14U;
    value.gid[0] = 0xfeU;
    value.gid[1] = 0x80U;
    return value;
}

}  // namespace

TEST(TransportCodecTest, ParsesTcpExchangeAddress) {
    const auto parsed = nds::parse_tcp_address("192.168.100.100:18515");
    ASSERT_TRUE(parsed);
    EXPECT_EQ(parsed->ipv4, "192.168.100.100");
    EXPECT_EQ(parsed->port, 18515U);

    EXPECT_FALSE(nds::parse_tcp_address("192.168.100.100"));
    EXPECT_FALSE(nds::parse_tcp_address("192.168.100.100:0"));
    EXPECT_FALSE(nds::parse_tcp_address("host:18515"));
}

TEST(TransportCodecTest, RoundTripsTransportRecord) {
    const nds::transport::QpInfo source = endpoint();
    nds::transport::QpInfo decoded{};
    nds::wire::QpInfo wire{};
    ASSERT_EQ(nds::transport::encode(&source, &wire), nds::transport::CodecResult::Ok);
    EXPECT_EQ(ntohl(wire.magic), nds::wire::kQpInfoMagic);
    EXPECT_EQ(ntohs(wire.version), nds::wire::kQpInfoVersion);
    ASSERT_EQ(nds::transport::decode(&wire, &decoded), nds::transport::CodecResult::Ok);
    EXPECT_EQ(std::memcmp(&source, &decoded, sizeof(source)), 0);

    wire.magic = 0U;
    EXPECT_NE(nds::transport::decode(&wire, &decoded), nds::transport::CodecResult::Ok);
}
