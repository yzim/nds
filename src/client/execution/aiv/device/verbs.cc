#include "api.h"
#include "internal.h"
#include "hns_hw.h"

namespace {
struct HnsRoceRcSqWqe {
    uint32_t byte_4;
    uint32_t message_length;
    uint32_t immediate_data;
    uint32_t sge_count;
    uint32_t start_sge_index;
    uint32_t remote_key;
    uint64_t remote_address;
};

struct HnsRoceSge {
    uint32_t length;
    uint32_t local_key;
    uint64_t local_address;
};

__aicore__ inline void StoreU64(uint64_t address, uint64_t value, TBuf<> *scratch) {
    LocalTensor<uint64_t> local = scratch->GetWithOffset<uint64_t>(1U, 0U);
    local.SetValue(0U, value);
    GlobalTensor<uint64_t> global;
    global.SetGlobalBuffer(reinterpret_cast<__gm__ uint64_t *>(address));
    DataCopyExtParams params{1U, sizeof(uint64_t), 0U, 0U, 0U};
    DataCopyPad(global, local, params);
}

__aicore__ inline void StoreU32(uint64_t address, uint32_t value) {
    __gm__ uint32_t *destination = reinterpret_cast<__gm__ uint32_t *>(address);
    *destination = value;
    NdsAivCacheSync(reinterpret_cast<__gm__ uint8_t *>(destination), sizeof(value));
}

/* Copies a device-global WR into local memory before inspecting its fields. */
__aicore__ inline NdsDeviceSendWr LoadSendWr(__gm__ const NdsDeviceSendWr *wr) {
    NdsDeviceSendWr local{};
    local.wr_id = wr->wr_id;
    local.opcode = wr->opcode;
    local.flags = wr->flags;
    local.local.address = wr->local.address;
    local.local.length = wr->local.length;
    local.local.local_key = wr->local.local_key;
    local.remote_address = wr->remote_address;
    local.remote_key = wr->remote_key;
    local.reserved = wr->reserved;
    return local;
}

/* Publishes the first unposted WR address for a partially posted batch. */
__aicore__ inline void SetBadWrAddress(__gm__ uint64_t *bad_wr_address, uint64_t value) {
    *bad_wr_address = value;
    NdsAivCacheSync(reinterpret_cast<__gm__ uint8_t *>(bad_wr_address), sizeof(*bad_wr_address));
}

/* Validates the normalized opcodes accepted by the AIV send path. */
__aicore__ inline bool ValidSendWr(const NdsDeviceSendWr *wr) {
    return wr != nullptr && (wr->opcode == NDS_DEVICE_WR_SEND || wr->opcode == NDS_DEVICE_WR_RDMA_READ ||
                             wr->opcode == NDS_DEVICE_WR_RDMA_WRITE);
}

/* Populates and publishes one SQ WQE after its body and WR ID are visible. */
__aicore__ inline void PopulateSendWqe(__gm__ const NdsDeviceWorkQueue *queue, uint32_t producer,
                                       const NdsDeviceSendWr *wr) {
    __gm__ uint8_t *wqe_address = reinterpret_cast<__gm__ uint8_t *>(
        queue->buffer_address + (uint64_t)queue->entry_size * (producer % queue->depth));
    __gm__ HnsRoceRcSqWqe *wqe = reinterpret_cast<__gm__ HnsRoceRcSqWqe *>(wqe_address);
    const uint32_t hns_opcode = NDS_HNS_HW_SQ_OPCODE_FROM_DEVICE(wr->opcode);
    const uint32_t signaled = (wr->flags & NDS_DEVICE_SEND_SIGNALED) != 0U ? NDS_HNS_HW_SQ_SIGNALED : 0U;
    wqe->byte_4 = hns_opcode | signaled;
    wqe->message_length = wr->local.length;
    wqe->immediate_data = 0U;
    wqe->sge_count = 1U << 24U;
    wqe->start_sge_index = 0U;
    wqe->remote_key = wr->opcode == NDS_DEVICE_WR_SEND ? 0U : wr->remote_key;
    wqe->remote_address = wr->opcode == NDS_DEVICE_WR_SEND ? 0U : wr->remote_address;
    __gm__ HnsRoceSge *sge = reinterpret_cast<__gm__ HnsRoceSge *>(wqe_address + sizeof(HnsRoceRcSqWqe));
    sge->length = wr->local.length;
    sge->local_key = wr->local.local_key;
    sge->local_address = wr->local.address;
    __gm__ uint64_t *wr_id = &reinterpret_cast<__gm__ uint64_t *>(queue->wr_id_address)[producer % queue->depth];
    *wr_id = wr->wr_id;
    NdsAivCacheSync(reinterpret_cast<__gm__ uint8_t *>(wr_id), sizeof(*wr_id));
    NdsAivCacheSync(wqe_address, sizeof(HnsRoceRcSqWqe) + sizeof(HnsRoceSge));
    PipeBarrier<PIPE_ALL>();
    const uint32_t owner = (producer >> 15U) & 1U;
    wqe->byte_4 |= ((~owner) << 7U) & (1U << 7U);
    NdsAivCacheSync(reinterpret_cast<__gm__ uint8_t *>(&wqe->byte_4), sizeof(wqe->byte_4));
}

/* Advances the SQ producer and rings one doorbell for the populated prefix. */
__aicore__ inline void RingSendDoorbell(__gm__ const NdsDeviceQp *qp, __gm__ const NdsDeviceWorkQueue *queue,
                                        uint32_t producer, TBuf<> *scratch) {
    StoreU32(queue->head_address, producer);
    PipeBarrier<PIPE_ALL>();
    const uint64_t doorbell =
        (uint64_t)queue->number | ((uint64_t)(producer & 0xffffU) << 32U) | ((uint64_t)qp->service_level << 48U);
    StoreU64(queue->doorbell_address, doorbell, scratch);
}
}  // namespace

NDS_AIV_DEVICE_API_LINKAGE __aicore__ void nds_aiv_post_send(__gm__ const NdsDeviceQp *qp, const NdsDeviceSendWr *wr,
                                                             __gm__ int32_t *return_value, TBuf<> *scratch) {
    if (return_value == nullptr)
        return;
    if (!NdsAivValidQp(qp) || !ValidSendWr(wr) || scratch == nullptr) {
        NdsAivSetReturnValue(return_value, NDS_DEVICE_OPERATION_INVALID_ARGUMENT);
        return;
    }
    __gm__ const NdsDeviceWorkQueue *queue = &qp->send_queue;
    __gm__ uint32_t *head_address = reinterpret_cast<__gm__ uint32_t *>(queue->head_address);
    __gm__ uint32_t *tail_address = reinterpret_cast<__gm__ uint32_t *>(queue->tail_address);
    NdsAivCacheSync(reinterpret_cast<__gm__ uint8_t *>(head_address), sizeof(uint32_t));
    const uint32_t head = *head_address;
    NdsAivCacheSync(reinterpret_cast<__gm__ uint8_t *>(tail_address), sizeof(uint32_t));
    if (queue->depth <= 1U || (head - *tail_address) >= queue->depth - 1U) {
        NdsAivSetReturnValue(return_value, NDS_DEVICE_OPERATION_QUEUE_FULL);
        return;
    }
    PopulateSendWqe(queue, head, wr);
    RingSendDoorbell(qp, queue, head + 1U, scratch);
    NdsAivSetReturnValue(return_value, NDS_DEVICE_OPERATION_SUCCESS);
}

/* Posts a contiguous WR array, then rings once for its successfully posted prefix. */
NDS_AIV_DEVICE_API_LINKAGE __aicore__ void nds_aiv_post_send_batch(__gm__ const NdsDeviceQp *qp,
                                                                   __gm__ const NdsDeviceSendWr *wrs, uint32_t wr_count,
                                                                   __gm__ int32_t *return_value,
                                                                   __gm__ uint64_t *bad_wr_address, TBuf<> *scratch) {
    if (return_value == nullptr)
        return;
    if (bad_wr_address == nullptr) {
        NdsAivSetReturnValue(return_value, NDS_DEVICE_OPERATION_INVALID_ARGUMENT);
        return;
    }
    SetBadWrAddress(bad_wr_address, 0U);
    if (!NdsAivValidQp(qp) || wrs == nullptr || wr_count == 0U || scratch == nullptr) {
        if (wrs != nullptr && wr_count != 0U)
            SetBadWrAddress(bad_wr_address, reinterpret_cast<uint64_t>(wrs));
        NdsAivSetReturnValue(return_value, NDS_DEVICE_OPERATION_INVALID_ARGUMENT);
        return;
    }
    __gm__ const NdsDeviceWorkQueue *queue = &qp->send_queue;
    __gm__ uint32_t *head_address = reinterpret_cast<__gm__ uint32_t *>(queue->head_address);
    __gm__ uint32_t *tail_address = reinterpret_cast<__gm__ uint32_t *>(queue->tail_address);
    NdsAivCacheSync(reinterpret_cast<__gm__ uint8_t *>(head_address), sizeof(uint32_t));
    const uint32_t head = *head_address;
    NdsAivCacheSync(reinterpret_cast<__gm__ uint8_t *>(tail_address), sizeof(uint32_t));
    const uint32_t used = head - *tail_address;
    if (queue->depth <= 1U || used >= queue->depth - 1U) {
        SetBadWrAddress(bad_wr_address, reinterpret_cast<uint64_t>(wrs));
        NdsAivSetReturnValue(return_value, NDS_DEVICE_OPERATION_QUEUE_FULL);
        return;
    }
    uint32_t posted = 0U;
    uint32_t status = NDS_DEVICE_OPERATION_SUCCESS;
    for (; posted < wr_count; ++posted) {
        NdsDeviceSendWr local = LoadSendWr(&wrs[posted]);
        if (!ValidSendWr(&local)) {
            status = NDS_DEVICE_OPERATION_INVALID_ARGUMENT;
            SetBadWrAddress(bad_wr_address, reinterpret_cast<uint64_t>(&wrs[posted]));
            break;
        }
        if (posted >= queue->depth - 1U - used) {
            status = NDS_DEVICE_OPERATION_QUEUE_FULL;
            SetBadWrAddress(bad_wr_address, reinterpret_cast<uint64_t>(&wrs[posted]));
            break;
        }
        PopulateSendWqe(queue, head + posted, &local);
    }
    if (posted != 0U)
        RingSendDoorbell(qp, queue, head + posted, scratch);
    NdsAivSetReturnValue(return_value, status);
}

NDS_AIV_DEVICE_API_LINKAGE __aicore__ void nds_aiv_post_recv(__gm__ const NdsDeviceQp *qp, const NdsDeviceRecvWr *wr,
                                                             __gm__ int32_t *return_value) {
    if (return_value == nullptr)
        return;
    if (!NdsAivValidQp(qp) || wr == nullptr) {
        NdsAivSetReturnValue(return_value, NDS_DEVICE_OPERATION_INVALID_ARGUMENT);
        return;
    }
    __gm__ const NdsDeviceWorkQueue *queue = &qp->receive_queue;
    __gm__ uint32_t *head_address = reinterpret_cast<__gm__ uint32_t *>(queue->head_address);
    __gm__ uint32_t *tail_address = reinterpret_cast<__gm__ uint32_t *>(queue->tail_address);
    NdsAivCacheSync(reinterpret_cast<__gm__ uint8_t *>(head_address), sizeof(uint32_t));
    NdsAivCacheSync(reinterpret_cast<__gm__ uint8_t *>(tail_address), sizeof(uint32_t));
    const uint32_t head = *head_address;
    if ((head - *tail_address) >= queue->depth) {
        NdsAivSetReturnValue(return_value, NDS_DEVICE_OPERATION_QUEUE_FULL);
        return;
    }
    __gm__ HnsRoceSge *sge = reinterpret_cast<__gm__ HnsRoceSge *>(queue->buffer_address +
                                                                   (uint64_t)queue->entry_size * (head % queue->depth));
    sge->length = wr->local.length;
    sge->local_key = wr->local.local_key;
    sge->local_address = wr->local.address;
    __gm__ uint64_t *wr_id = &reinterpret_cast<__gm__ uint64_t *>(queue->wr_id_address)[head % queue->depth];
    *wr_id = wr->wr_id;
    NdsAivCacheSync(reinterpret_cast<__gm__ uint8_t *>(wr_id), sizeof(*wr_id));
    NdsAivCacheSync(reinterpret_cast<__gm__ uint8_t *>(sge), sizeof(HnsRoceSge));
    PipeBarrier<PIPE_ALL>();
    const uint32_t next = head + 1U;
    StoreU32(queue->doorbell_address, next & 0xffffU);
    StoreU32(queue->head_address, next);
    NdsAivSetReturnValue(return_value, NDS_DEVICE_OPERATION_SUCCESS);
}

NDS_AIV_DEVICE_API_LINKAGE __aicore__ void nds_aiv_poll_cq(__gm__ const NdsDeviceQp *qp, uint32_t is_send_cq_value,
                                                           uint32_t max_completions, __gm__ NdsDeviceWc *wc,
                                                           __gm__ int32_t *return_value) {
    if (!NdsAivValidQp(qp) || is_send_cq_value > 1U || max_completions == 0U || wc == nullptr ||
        return_value == nullptr) {
        NdsAivSetReturnValue(return_value, NDS_DEVICE_OPERATION_INVALID_ARGUMENT);
        return;
    }
    const bool is_send_cq = is_send_cq_value != 0U;
    __gm__ const NdsDeviceCq *cq = is_send_cq ? &qp->send_cq : &qp->receive_cq;
    __gm__ const NdsDeviceWorkQueue *wq = is_send_cq ? &qp->send_queue : &qp->receive_queue;
    __gm__ uint32_t *consumer_address = reinterpret_cast<__gm__ uint32_t *>(cq->consumer_address);
    __gm__ uint32_t *tail_address = reinterpret_cast<__gm__ uint32_t *>(wq->tail_address);
    NdsAivCacheSync(reinterpret_cast<__gm__ uint8_t *>(consumer_address), sizeof(uint32_t));
    uint32_t consumer = *consumer_address;
    NdsAivCacheSync(reinterpret_cast<__gm__ uint8_t *>(tail_address), sizeof(uint32_t));
    uint32_t tail = *tail_address;
    uint32_t count = 0U;
    const uint32_t limit = max_completions < NDS_DEVICE_MAX_COMPLETIONS ? max_completions : NDS_DEVICE_MAX_COMPLETIONS;
    while (count < limit) {
        __gm__ NdsHnsHwCqe *cqe = reinterpret_cast<__gm__ NdsHnsHwCqe *>(
            cq->buffer_address + (uint64_t)cq->entry_size * (consumer % cq->depth));
        NdsAivCacheSync(reinterpret_cast<__gm__ uint8_t *>(cqe), sizeof(NdsHnsHwCqe));
        const uint32_t owner = (cqe->byte_4 >> 7U) & 1U;
        if ((owner ^ !!(consumer & cq->depth)) == 0U)
            break;
        const uint32_t wqe_index = cqe->byte_4 >> 16U;
        if (is_send_cq)
            tail += (wqe_index - tail) & (wq->depth - 1U);
        __gm__ NdsDeviceWc *completion = &wc[count++];
        completion->wr_id = reinterpret_cast<__gm__ uint64_t *>(wq->wr_id_address)[tail % wq->depth];
        completion->status = (cqe->byte_4 >> 8U) & 0xffU;
        completion->opcode = cqe->byte_4 & 0x1fU;
        completion->vendor_error = (cqe->byte_16 >> 24U) & 0xffU;
        completion->byte_length = cqe->byte_count;
        completion->qp_number = cqe->byte_12 & 0x00ffffffU;
        completion->flags = 0U;
        completion->immediate_data_or_invalidated_rkey = cqe->immediate_data;
        completion->reserved = 0U;
        ++consumer;
        ++tail;
    }
    StoreU32(reinterpret_cast<uint64_t>(return_value), count);
    if (count != 0U) {
        StoreU32(cq->consumer_address, consumer);
        StoreU32(wq->tail_address, tail);
        StoreU32(cq->doorbell_address, consumer & 0x00ffffffU);
    }
}
