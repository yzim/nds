#include "transport_protocol.hh"

#include <gtest/gtest.h>

TEST(TransportMtuTest, UsesLocalActiveMtuPolicy) {
    static const uint32_t supported[] = {256U, 512U, 1024U, 2048U, 4096U};

    for (const uint32_t mtu : supported) {
        EXPECT_NE(nds::transport::mtu_is_supported(mtu), 0);
        EXPECT_EQ(nds::transport::select_mtu(mtu, 1024U), mtu);
    }
    EXPECT_EQ(nds::transport::mtu_is_supported(0U), 0);
    EXPECT_EQ(nds::transport::mtu_is_supported(1536U), 0);
    EXPECT_EQ(nds::transport::select_mtu(1536U, 4096U), 0U);
    EXPECT_EQ(nds::transport::select_mtu(4096U, 1024U), 4096U);
}
