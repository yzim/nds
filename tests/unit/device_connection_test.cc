#include "nds/device_operations.h"

#include <cassert>
#include <cstddef>
#include <cstdint>

int main() {
    nds_device_connection connection{};
    connection.abi_version = NDS_DEVICE_CONNECTION_ABI_VERSION;
    connection.size = sizeof(connection);
    connection.qp.abi_version = NDS_DEVICE_QP_ABI_VERSION;
    connection.qp.size = sizeof(connection.qp);
    connection.qp.provider_qp_address = UINT64_C(0x12340000);

    nds_device_transfer transfer{UINT64_C(0x55),
                                 {UINT64_C(0x1000), 4096U, UINT32_C(0x77)},
                                 UINT64_C(0x2000), UINT32_C(0x88), 0U};
    nds_device_send_wr send{};
    nds_device_build_send_wr(&transfer, NDS_DEVICE_WR_RDMA_WRITE, &send);
    assert(send.wr_id == transfer.wr_id);
    assert(send.opcode == NDS_DEVICE_WR_RDMA_WRITE);
    assert(send.flags == NDS_DEVICE_SEND_SIGNALED);
    assert(send.local.address == transfer.local.address);
    assert(send.remote_address == transfer.remote_address);
    assert(send.remote_key == transfer.remote_key);

    nds_device_recv_wr receive{};
    nds_device_build_recv_wr(&transfer, &receive);
    assert(receive.wr_id == transfer.wr_id);
    assert(receive.local.length == transfer.local.length);
    assert(sizeof(nds_device_qp) == 240U);
    assert(sizeof(nds_device_connection) == 248U);
    assert(sizeof(nds_device_operation_request) == 312U);
    assert(offsetof(nds_device_operation_request, connection) == 16U);
    return 0;
}
