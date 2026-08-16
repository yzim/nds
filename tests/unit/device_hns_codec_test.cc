#include "nds/device_hns_codec.h"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

namespace {

void test_receive_segment_hardware_layout() {
    nds_hns_receive_segment segment{};
    nds_hns_encode_receive_segment(&segment, UINT64_C(0x1122334455667788),
                                   UINT32_C(0xa1b2c3d4), UINT32_C(0x55667788));
    assert(segment.length == UINT32_C(0xa1b2c3d4));
    assert(segment.local_key == UINT32_C(0x55667788));
    assert(segment.address == UINT64_C(0x1122334455667788));
    assert(offsetof(nds_hns_receive_segment, length) == 0U);
    assert(offsetof(nds_hns_receive_segment, local_key) == 4U);
    assert(offsetof(nds_hns_receive_segment, address) == 8U);

    std::uint8_t bytes[sizeof(segment)]{};
    std::memcpy(bytes, &segment, sizeof(segment));
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    assert(bytes[0] == 0xd4U && bytes[4] == 0x88U && bytes[8] == 0x88U && bytes[15] == 0x11U);
#endif
}

void test_queue_capacity_and_counter_wrap() {
    assert(nds_hns_queue_has_space(0U, 0U, 8U, 0U));
    assert(nds_hns_queue_has_space(7U, 0U, 8U, 0U));
    assert(!nds_hns_queue_has_space(8U, 0U, 8U, 0U));
    assert(nds_hns_queue_has_space(6U, 0U, 8U, 1U));
    assert(!nds_hns_queue_has_space(7U, 0U, 8U, 1U));
    assert(!nds_hns_queue_has_space(0U, 0U, 0U, 0U));
    assert(!nds_hns_queue_has_space(0U, 0U, 1U, 1U));
    assert(nds_hns_queue_has_space(1U, std::numeric_limits<std::uint32_t>::max() - 1U,
                                   8U, 0U));
}

void test_cqe_owner_phase() {
    nds_hns_cqe cqe{};
    cqe.byte_4 = 1U << 7U;
    assert(nds_hns_cqe_is_ready(&cqe, 0U, 8U));
    assert(!nds_hns_cqe_is_ready(&cqe, 8U, 8U));
    cqe.byte_4 = 0U;
    assert(!nds_hns_cqe_is_ready(&cqe, 0U, 8U));
    assert(nds_hns_cqe_is_ready(&cqe, 8U, 8U));
    assert(!nds_hns_cqe_is_ready(&cqe, 0U, 0U));
}

void test_send_tail_wrap() {
    nds_hns_cqe cqe{};
    cqe.byte_4 = 1U << 16U;
    assert(nds_hns_send_tail_for_cqe(0U, 8U, &cqe) == 1U);
    assert(nds_hns_send_tail_for_cqe(6U, 8U, &cqe) == 9U);
    cqe.byte_4 = 7U << 16U;
    assert(nds_hns_send_tail_for_cqe(9U, 8U, &cqe) == 15U);
}

void test_completion_decode() {
    nds_hns_cqe cqe{};
    cqe.byte_4 = (UINT32_C(0x4321) << 16U) | (UINT32_C(0x5a) << 8U) | 0x13U;
    cqe.immediate_data = UINT32_C(0xaabbccdd);
    cqe.byte_12 = UINT32_C(0xff345678);
    cqe.byte_16 = UINT32_C(0x7e000000);
    cqe.byte_count = 4096U;
    nds_device_completion completion{};
    nds_hns_decode_cqe(&cqe, UINT64_C(0x1020304050607080), &completion);
    assert(completion.wr_id == UINT64_C(0x1020304050607080));
    assert(completion.status == 0x5a);
    assert(completion.opcode == 0x13);
    assert(completion.vendor_error == 0x7eU);
    assert(completion.byte_length == 4096U);
    assert(completion.qp_number == UINT32_C(0x345678));
    assert(completion.flags == 0U);
    assert(completion.immediate_data_or_invalidated_rkey == UINT32_C(0xaabbccdd));
}

}  // namespace

int main() {
    assert(NDS_HNS_SQ_OPCODE_FROM_DEVICE(NDS_DEVICE_WR_SEND) == NDS_HNS_SQ_SEND);
    assert(NDS_HNS_SQ_OPCODE_FROM_DEVICE(NDS_DEVICE_WR_RDMA_WRITE) == NDS_HNS_SQ_RDMA_WRITE);
    assert(NDS_HNS_SQ_OPCODE_FROM_DEVICE(NDS_DEVICE_WR_RDMA_READ) == NDS_HNS_SQ_RDMA_READ);
    assert(NDS_HNS_SQ_OPCODE_FROM_DEVICE(99U) == NDS_HNS_SQ_INVALID);

    test_receive_segment_hardware_layout();
    test_queue_capacity_and_counter_wrap();
    test_cqe_owner_phase();
    test_send_tail_wrap();
    test_completion_decode();
    return 0;
}
