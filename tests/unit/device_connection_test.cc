#include "nds/device_operations.h"

#include <gtest/gtest.h>
#include <cstdint>

TEST(DeviceConnectionTest, BuildsWorkRequests) {
    NdsDeviceTransfer transfer{
        UINT64_C(0x55), {UINT64_C(0x1000), 4096U, UINT32_C(0x77)}, UINT64_C(0x2000), UINT32_C(0x88), 0U};
    NdsDeviceSendWr send{};
    nds_device_build_send_wr(&transfer, NDS_DEVICE_WR_RDMA_WRITE, &send);
    EXPECT_TRUE(send.wr_id == transfer.wr_id);
    EXPECT_TRUE(send.opcode == NDS_DEVICE_WR_RDMA_WRITE);
    EXPECT_TRUE(send.flags == NDS_DEVICE_SEND_SIGNALED);
    EXPECT_TRUE(send.local.address == transfer.local.address);
    EXPECT_TRUE(send.remote_address == transfer.remote_address);
    EXPECT_TRUE(send.remote_key == transfer.remote_key);

    NdsDeviceRecvWr receive{};
    nds_device_build_recv_wr(&transfer, &receive);
    EXPECT_TRUE(receive.wr_id == transfer.wr_id);
    EXPECT_TRUE(receive.local.length == transfer.local.length);
}
