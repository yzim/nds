#include "nds/device_operations.h"

#include <gtest/gtest.h>
#include <cstddef>
#include <cstdint>

TEST(DeviceConnectionTest, BuildsWorkRequestsAndPreservesAbi) {
    nds_device_transport transport{};
    transport.abi_version = NDS_DEVICE_TRANSPORT_ABI_VERSION;
    transport.size = sizeof(transport);
    transport.control_qp.abi_version = NDS_DEVICE_QP_ABI_VERSION;
    transport.control_qp.size = sizeof(transport.control_qp);
    transport.control_qp.provider_qp_address = UINT64_C(0x12340000);

    nds_device_transfer transfer{
        UINT64_C(0x55), {UINT64_C(0x1000), 4096U, UINT32_C(0x77)}, UINT64_C(0x2000), UINT32_C(0x88), 0U};
    nds_device_send_wr send{};
    nds_device_build_send_wr(&transfer, NDS_DEVICE_WR_RDMA_WRITE, &send);
    EXPECT_TRUE(send.wr_id == transfer.wr_id);
    EXPECT_TRUE(send.opcode == NDS_DEVICE_WR_RDMA_WRITE);
    EXPECT_TRUE(send.flags == NDS_DEVICE_SEND_SIGNALED);
    EXPECT_TRUE(send.local.address == transfer.local.address);
    EXPECT_TRUE(send.remote_address == transfer.remote_address);
    EXPECT_TRUE(send.remote_key == transfer.remote_key);

    nds_device_recv_wr receive{};
    nds_device_build_recv_wr(&transfer, &receive);
    EXPECT_TRUE(receive.wr_id == transfer.wr_id);
    EXPECT_TRUE(receive.local.length == transfer.local.length);
    EXPECT_TRUE(sizeof(nds_device_qp) == 240U);
    EXPECT_TRUE(sizeof(nds_device_transport) == 248U);
    EXPECT_TRUE(sizeof(nds_device_operation_request) == 312U);
    EXPECT_TRUE(offsetof(nds_device_operation_request, transport) == 16U);
}
