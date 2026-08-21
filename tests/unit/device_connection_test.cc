#include "nds/device_verbs.h"

#include <gtest/gtest.h>
#include <cstdint>

TEST(DeviceConnectionTest, BuildsWorkRequests) {
    const NdsDeviceSendWr send{
        UINT64_C(0x55), NDS_DEVICE_WR_RDMA_WRITE, NDS_DEVICE_SEND_SIGNALED,
        {UINT64_C(0x1000), 4096U, UINT32_C(0x77)}, UINT64_C(0x2000), UINT32_C(0x88), 0U};
    EXPECT_TRUE(send.wr_id == UINT64_C(0x55));
    EXPECT_TRUE(send.opcode == NDS_DEVICE_WR_RDMA_WRITE);
    EXPECT_TRUE(send.flags == NDS_DEVICE_SEND_SIGNALED);
    EXPECT_TRUE(send.local.address == UINT64_C(0x1000));
    EXPECT_TRUE(send.local.length == 4096U);
    EXPECT_TRUE(send.local.local_key == UINT32_C(0x77));
    EXPECT_TRUE(send.remote_address == UINT64_C(0x2000));
    EXPECT_TRUE(send.remote_key == UINT32_C(0x88));

    const NdsDeviceRecvWr receive{UINT64_C(0x55), {UINT64_C(0x1000), 4096U, UINT32_C(0x77)}};
    EXPECT_TRUE(receive.wr_id == UINT64_C(0x55));
    EXPECT_TRUE(receive.local.length == 4096U);
    EXPECT_TRUE(receive.local.local_key == UINT32_C(0x77));
}
