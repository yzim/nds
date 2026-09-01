#include "device_verbs.h"
#include "device_transport.h"

#include <gtest/gtest.h>
#include <cstdint>

TEST(DeviceConnectionTest, BuildsWorkRequests) {
    const NdsDeviceSendWr send{UINT64_C(0x55),
                               NDS_DEVICE_WR_RDMA_WRITE,
                               NDS_DEVICE_SEND_SIGNALED,
                               {UINT64_C(0x1000), 4096U, UINT32_C(0x77)},
                               UINT64_C(0x2000),
                               UINT32_C(0x88),
                               0U};
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

TEST(DeviceConnectionTest, DefinesBatchPostSendEnvelope) {
    NdsDevicePostSendBatchArgs args{};
    args.wrs_address = UINT64_C(0x1000);
    args.wr_count = 64U;
    args.bad_wr_address = UINT64_C(0x1060);

    EXPECT_EQ(args.wrs_address, UINT64_C(0x1000));
    EXPECT_EQ(args.wr_count, 64U);
    EXPECT_EQ(args.bad_wr_address, UINT64_C(0x1060));
    EXPECT_EQ(sizeof(args), 272U);
}

TEST(DeviceConnectionTest, DefinesDirectOperatorResultEnvelopes) {
    constexpr std::int32_t kResult = -3;

    NdsDevicePostSendArgs post_send{};
    NdsDevicePostRecvArgs post_recv{};
    NdsDevicePollCqArgs poll_cq{};
    NdsDeviceRdmaSendArgs rdma_send{};
    NdsDeviceRdmaRecvArgs rdma_recv{};
    NdsDeviceRdmaReadArgs rdma_read{};
    NdsDeviceRdmaWriteArgs rdma_write{};
    post_send.return_value = kResult;
    post_recv.return_value = kResult;
    poll_cq.return_value = kResult;
    rdma_send.return_value = kResult;
    rdma_recv.return_value = kResult;
    rdma_read.return_value = kResult;
    rdma_write.return_value = kResult;

    EXPECT_EQ(post_send.return_value, kResult);
    EXPECT_EQ(post_recv.return_value, kResult);
    EXPECT_EQ(poll_cq.return_value, kResult);
    EXPECT_EQ(rdma_send.return_value, kResult);
    EXPECT_EQ(rdma_recv.return_value, kResult);
    EXPECT_EQ(rdma_read.return_value, kResult);
    EXPECT_EQ(rdma_write.return_value, kResult);
}
