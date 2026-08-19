#include "nds/protocol.h"
#include "nds/connection.h"

#include <arpa/inet.h>

#include <cstring>

#include <gtest/gtest.h>

namespace {

nds_qp_info endpoint() {
    nds_qp_info value{};
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

TEST(TransportCodecTest, RoundTripsTransportAndStorageRecords) {
    const nds_qp_info source = endpoint();
    nds_qp_info decoded{};
    nds_qp_info_wire wire{};
    ASSERT_EQ(nds_qp_info_encode(&source, &wire), NDS_QP_INFO_RESULT_OK);
    EXPECT_EQ(ntohl(wire.magic), NDS_QP_INFO_WIRE_MAGIC);
    EXPECT_EQ(ntohs(wire.version), NDS_QP_INFO_WIRE_VERSION);
    ASSERT_EQ(nds_qp_info_decode(&wire, &decoded), NDS_QP_INFO_RESULT_OK);
    EXPECT_EQ(std::memcmp(&source, &decoded, sizeof(source)), 0);

    nds_protocol_bootstrap bootstrap_source{{0x0102030405060708U, 64U, 0x12345678U, NDS_PROTOCOL_ACCESS_REMOTE_WRITE}};
    nds_protocol_bootstrap bootstrap_decoded{};
    nds_protocol_bootstrap_wire bootstrap_wire{};
    ASSERT_EQ(nds_protocol_bootstrap_encode(&bootstrap_source, &bootstrap_wire), NDS_PROTOCOL_RESULT_OK);
    ASSERT_EQ(nds_protocol_bootstrap_decode(&bootstrap_wire, &bootstrap_decoded), NDS_PROTOCOL_RESULT_OK);
    EXPECT_EQ(std::memcmp(&bootstrap_source, &bootstrap_decoded, sizeof(bootstrap_source)), 0);

    nds_protocol_namespace namespace_source{1024U * 1024U};
    nds_protocol_namespace namespace_decoded{};
    nds_protocol_namespace_wire namespace_wire{};
    ASSERT_EQ(nds_protocol_namespace_encode(&namespace_source, &namespace_wire), NDS_PROTOCOL_RESULT_OK);
    ASSERT_EQ(nds_protocol_namespace_decode(&namespace_wire, &namespace_decoded), NDS_PROTOCOL_RESULT_OK);
    EXPECT_EQ(namespace_source.capacity, namespace_decoded.capacity);

    nds_protocol_command command_source{0x1020304050607080U,
                                        NDS_PROTOCOL_READ,
                                        4096U,
                                        8192U,
                                        {0x1020304050607080U, 8192U, 0x12345678U, NDS_PROTOCOL_ACCESS_REMOTE_WRITE}};
    nds_protocol_command command_decoded{};
    nds_protocol_command_wire command_wire{};
    ASSERT_EQ(nds_protocol_command_encode(&command_source, &command_wire), NDS_PROTOCOL_RESULT_OK);
    ASSERT_EQ(nds_protocol_command_decode(&command_wire, &command_decoded), NDS_PROTOCOL_RESULT_OK);
    EXPECT_EQ(std::memcmp(&command_source, &command_decoded, sizeof(command_source)), 0);
    command_wire.data_access = htonl(NDS_PROTOCOL_ACCESS_REMOTE_READ);
    EXPECT_NE(nds_protocol_command_decode(&command_wire, &command_decoded), NDS_PROTOCOL_RESULT_OK);

    nds_protocol_completion completion_source{0x1020304050607080U, NDS_PROTOCOL_COMPLETION_COMPLETE,
                                              NDS_PROTOCOL_SUCCESS, 8192U};
    nds_protocol_completion completion_decoded{};
    nds_protocol_completion_wire completion_wire{};
    ASSERT_EQ(nds_protocol_completion_encode(&completion_source, &completion_wire), NDS_PROTOCOL_RESULT_OK);
    ASSERT_EQ(nds_protocol_completion_decode(&completion_wire, &completion_decoded), NDS_PROTOCOL_RESULT_OK);
    EXPECT_EQ(std::memcmp(&completion_source, &completion_decoded, sizeof(completion_source)), 0);

    wire.magic = 0U;
    EXPECT_NE(nds_qp_info_decode(&wire, &decoded), NDS_QP_INFO_RESULT_OK);
}
