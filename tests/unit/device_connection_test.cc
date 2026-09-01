#include "backend_verbs.h"
#include "backend_transport.h"

#include <gtest/gtest.h>
#include <cstdint>

TEST(DeviceConnectionTest, BuildsWorkRequests) {
    const NdsSendWr send{UINT64_C(0x55),
                         NDS_WR_RDMA_WRITE,
                         NDS_SEND_SIGNALED,
                         {UINT64_C(0x1000), 4096U, UINT32_C(0x77)},
                         UINT64_C(0x2000),
                         UINT32_C(0x88),
                         0U};
    EXPECT_TRUE(send.wr_id == UINT64_C(0x55));
    EXPECT_TRUE(send.opcode == NDS_WR_RDMA_WRITE);
    EXPECT_TRUE(send.flags == NDS_SEND_SIGNALED);
    EXPECT_TRUE(send.local.address == UINT64_C(0x1000));
    EXPECT_TRUE(send.local.length == 4096U);
    EXPECT_TRUE(send.local.local_key == UINT32_C(0x77));
    EXPECT_TRUE(send.remote_address == UINT64_C(0x2000));
    EXPECT_TRUE(send.remote_key == UINT32_C(0x88));

    const NdsRecvWr receive{UINT64_C(0x55), {UINT64_C(0x1000), 4096U, UINT32_C(0x77)}};
    EXPECT_TRUE(receive.wr_id == UINT64_C(0x55));
    EXPECT_TRUE(receive.local.length == 4096U);
    EXPECT_TRUE(receive.local.local_key == UINT32_C(0x77));
}

TEST(DeviceConnectionTest, DefinesBatchPostSendEnvelope) {
    NdsPostSendBatchArgs args{};
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

    NdsPostSendArgs post_send{};
    NdsPostRecvArgs post_recv{};
    NdsPollCqArgs poll_cq{};
    NdsRdmaSendArgs rdma_send{};
    NdsRdmaRecvArgs rdma_recv{};
    NdsRdmaReadArgs rdma_read{};
    NdsRdmaWriteArgs rdma_write{};
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

TEST(DeviceConnectionTest, IndexesTransportDescriptorsAndStatesTogether) {
    NdsQpDescriptor descriptors[2]{};
    NdsTransportQpState states[2]{};
    const NdsTransportDescriptor transport{reinterpret_cast<uint64_t>(descriptors), reinterpret_cast<uint64_t>(states),
                                           2U, 0U};

    EXPECT_EQ(nds_transport_qp(&transport, 1U), &descriptors[1]);
    EXPECT_EQ(nds_transport_qp_state(&transport, 1U), &states[1]);
    EXPECT_EQ(nds_transport_qp(&transport, 2U), nullptr);
    EXPECT_EQ(nds_transport_qp_state(&transport, 2U), nullptr);
}

TEST(DeviceConnectionTest, KeepsSignalScheduleAcrossCallsAndReclaimsCredits) {
    NdsTransportQpState state{4U, 0U, 8U, 4U};

    EXPECT_EQ(nds_transport_should_signal(&state), 0U);
    ASSERT_EQ(nds_transport_record_send(&state, 0U), 1U);
    EXPECT_EQ(state.unsignaled_count, 1U);
    EXPECT_EQ(state.send_credits, 7U);

    ASSERT_EQ(nds_transport_record_send(&state, 0U), 1U);
    ASSERT_EQ(nds_transport_record_send(&state, 0U), 1U);
    EXPECT_EQ(nds_transport_should_signal(&state), 1U);
    ASSERT_EQ(nds_transport_record_send(&state, 1U), 1U);
    EXPECT_EQ(state.unsignaled_count, 0U);
    EXPECT_EQ(state.send_credits, 4U);

    nds_transport_reclaim_send(&state, 1U);
    EXPECT_EQ(state.send_credits, 8U);
    nds_transport_reclaim_receive(&state, 2U);
    EXPECT_EQ(state.receive_credits, 6U);
}
