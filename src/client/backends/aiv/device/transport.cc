#include "api.h"
#include "internal.h"
#include "../../common/hns_hw.h"

namespace {

__aicore__ inline NdsWorkQueueDescriptor LoadWorkQueue(__gm__ const NdsWorkQueueDescriptor *source) {
    NdsWorkQueueDescriptor value{};
    value.number = source->number;
    value.depth = source->depth;
    value.entry_size = source->entry_size;
    value.doorbell_mode = source->doorbell_mode;
    value.buffer_address = source->buffer_address;
    value.head_address = source->head_address;
    value.tail_address = source->tail_address;
    value.doorbell_address = source->doorbell_address;
    value.wr_id_address = source->wr_id_address;
    return value;
}

__aicore__ inline NdsCqDescriptor LoadCq(__gm__ const NdsCqDescriptor *source) {
    NdsCqDescriptor value{};
    value.number = source->number;
    value.depth = source->depth;
    value.entry_size = source->entry_size;
    value.doorbell_mode = source->doorbell_mode;
    value.buffer_address = source->buffer_address;
    value.consumer_address = source->consumer_address;
    value.doorbell_address = source->doorbell_address;
    return value;
}

__aicore__ inline NdsQpDescriptor LoadQp(__gm__ const NdsQpDescriptor *source) {
    NdsQpDescriptor value{};
    value.flags = source->flags;
    value.qp_mode = source->qp_mode;
    value.service_level = source->service_level;
    value.reserved = source->reserved;
    value.provider_qp_address = source->provider_qp_address;
    value.provider_send_cq_address = source->provider_send_cq_address;
    value.provider_receive_cq_address = source->provider_receive_cq_address;
    value.send_queue = LoadWorkQueue(&source->send_queue);
    value.receive_queue = LoadWorkQueue(&source->receive_queue);
    value.send_cq = LoadCq(&source->send_cq);
    value.receive_cq = LoadCq(&source->receive_cq);
    value.host_runtime_address = source->host_runtime_address;
    value.host_qp_address = source->host_qp_address;
    return value;
}

__aicore__ inline NdsTransportQpState LoadState(__gm__ const NdsTransportQpState *source) {
    NdsTransportQpState value{};
    value.signal_interval = source->signal_interval;
    value.unsignaled_count = source->unsignaled_count;
    value.send_credits = source->send_credits;
    value.receive_credits = source->receive_credits;
    return value;
}

__aicore__ inline void StoreState(__gm__ NdsTransportQpState *destination, const NdsTransportQpState &value) {
    destination->signal_interval = value.signal_interval;
    destination->unsignaled_count = value.unsignaled_count;
    destination->send_credits = value.send_credits;
    destination->receive_credits = value.receive_credits;
    NdsAivCacheSync(reinterpret_cast<__gm__ uint8_t *>(destination), sizeof(value));
}

/* Reclaims device-visible CQEs without exposing completion records to callers. */
__aicore__ inline uint32_t ReclaimTransportCq(__gm__ const NdsQpDescriptor *qp, __gm__ NdsTransportQpState *state,
                                              uint32_t send_cq, uint32_t *reclaimed) {
    if (qp == nullptr || state == nullptr || reclaimed == nullptr)
        return NDS_OPERATION_INVALID_ARGUMENT;
    const __gm__ NdsCqDescriptor *cq = send_cq != 0U ? &qp->send_cq : &qp->receive_cq;
    const __gm__ NdsWorkQueueDescriptor *wq = send_cq != 0U ? &qp->send_queue : &qp->receive_queue;
    if (cq->buffer_address == 0U || cq->consumer_address == 0U || cq->doorbell_address == 0U || cq->depth == 0U ||
        cq->entry_size < sizeof(NdsHnsHwCqe) || wq->tail_address == 0U || wq->wr_id_address == 0U || wq->depth == 0U)
        return NDS_OPERATION_INVALID_ARGUMENT;

    __gm__ uint32_t *consumer_address = reinterpret_cast<__gm__ uint32_t *>(cq->consumer_address);
    __gm__ uint32_t *tail_address = reinterpret_cast<__gm__ uint32_t *>(wq->tail_address);
    NdsAivCacheSync(reinterpret_cast<__gm__ uint8_t *>(consumer_address), sizeof(uint32_t));
    NdsAivCacheSync(reinterpret_cast<__gm__ uint8_t *>(tail_address), sizeof(uint32_t));
    uint32_t consumer = *consumer_address;
    uint32_t tail = *tail_address;
    uint32_t count = 0U;
    uint32_t status = NDS_OPERATION_SUCCESS;
    while (count < NDS_MAX_COMPLETIONS) {
        __gm__ NdsHnsHwCqe *cqe = reinterpret_cast<__gm__ NdsHnsHwCqe *>(
            cq->buffer_address + (uint64_t)cq->entry_size * (consumer % cq->depth));
        NdsAivCacheSync(reinterpret_cast<__gm__ uint8_t *>(cqe), sizeof(NdsHnsHwCqe));
        if (!nds_hns_hw_cqe_is_ready(cqe, consumer, cq->depth))
            break;
        if (send_cq != 0U)
            tail = nds_hns_hw_send_tail_for_cqe(tail, wq->depth, cqe);
        if (((cqe->byte_4 >> 8U) & 0xffU) != 0U)
            status = NDS_OPERATION_PROVIDER_FAILED;
        ++consumer;
        ++tail;
        ++count;
    }
    if (count != 0U) {
        StoreU32(cq->consumer_address, consumer);
        StoreU32(wq->tail_address, tail);
        StoreU32(cq->doorbell_address, consumer & 0x00ffffffU);
        NdsTransportQpState next = LoadState(state);
        if (send_cq != 0U)
            next.send_credits += count * next.signal_interval;
        else
            next.receive_credits += count;
        StoreState(state, next);
    }
    *reclaimed = count;
    return status;
}

__aicore__ inline uint32_t ReclaimIfNeeded(__gm__ const NdsQpDescriptor *qp, __gm__ NdsTransportQpState *state,
                                           uint32_t send_cq) {
    const uint32_t credits = send_cq != 0U ? state->send_credits : state->receive_credits;
    if (credits != 0U)
        return NDS_OPERATION_SUCCESS;
    uint32_t reclaimed = 0U;
    return ReclaimTransportCq(qp, state, send_cq, &reclaimed);
}

__aicore__ inline uint32_t WaitForSendCompletion(__gm__ const NdsQpDescriptor *qp, __gm__ NdsTransportQpState *state,
                                                 uint32_t target_credits) {
    while (state->send_credits < target_credits) {
        uint32_t reclaimed = 0U;
        const uint32_t status = ReclaimTransportCq(qp, state, 1U, &reclaimed);
        if (status != NDS_OPERATION_SUCCESS)
            return status;
    }
    return NDS_OPERATION_SUCCESS;
}

__aicore__ inline uint32_t PostTransportSend(__gm__ const NdsTransportDescriptor *transport, uint32_t queue_index,
                                             const NdsSendWr *wr, __gm__ int32_t *return_value, TBuf<> *scratch) {
    if (transport == nullptr || wr == nullptr || return_value == nullptr || scratch == nullptr)
        return NDS_OPERATION_INVALID_ARGUMENT;
    __gm__ const NdsQpDescriptor *qp = nds_transport_qp_global(transport, queue_index);
    __gm__ NdsTransportQpState *state = nds_transport_qp_state_global(transport, queue_index);
    if (qp == nullptr || state == nullptr || state->signal_interval == 0U)
        return NDS_OPERATION_INVALID_ARGUMENT;
    uint32_t status = ReclaimIfNeeded(qp, state, 1U);
    if (status != NDS_OPERATION_SUCCESS)
        return status;
    if (state->send_credits == 0U)
        return NDS_OPERATION_QUEUE_FULL;

    NdsTransportQpState next = LoadState(state);
    const uint32_t signaled = next.unsignaled_count + 1U >= next.signal_interval ? 1U : 0U;
    NdsSendWr scheduled = *wr;
    scheduled.flags = signaled != 0U ? NDS_SEND_SIGNALED : 0U;
    const NdsQpDescriptor local_qp = LoadQp(qp);
    nds_aiv_post_send(&local_qp, &scheduled, return_value, scratch);
    if (*return_value != 0)
        return static_cast<uint32_t>(-*return_value);
    if (next.send_credits == 0U)
        return NDS_OPERATION_QUEUE_FULL;
    --next.send_credits;
    next.unsignaled_count = signaled != 0U ? 0U : next.unsignaled_count + 1U;
    StoreState(state, next);
    if (signaled != 0U)
        return WaitForSendCompletion(qp, state, state->send_credits + state->signal_interval);
    return NDS_OPERATION_SUCCESS;
}

__aicore__ inline uint32_t PostTransportSendBatch(__gm__ const NdsTransportDescriptor *transport, uint32_t queue_index,
                                                  __gm__ const NdsSendWr *wrs, uint32_t wr_count,
                                                  __gm__ int32_t *return_value, __gm__ uint64_t *bad_wr,
                                                  TBuf<> *scratch) {
    if (transport == nullptr || wrs == nullptr || wr_count == 0U || return_value == nullptr || bad_wr == nullptr ||
        scratch == nullptr)
        return NDS_OPERATION_INVALID_ARGUMENT;
    __gm__ const NdsQpDescriptor *qp = nds_transport_qp_global(transport, queue_index);
    __gm__ NdsTransportQpState *state = nds_transport_qp_state_global(transport, queue_index);
    if (qp == nullptr || state == nullptr || state->signal_interval == 0U)
        return NDS_OPERATION_INVALID_ARGUMENT;
    const __gm__ NdsWorkQueueDescriptor *queue = &qp->send_queue;
    __gm__ uint32_t *head_address = reinterpret_cast<__gm__ uint32_t *>(queue->head_address);
    __gm__ uint32_t *tail_address = reinterpret_cast<__gm__ uint32_t *>(queue->tail_address);
    if (head_address == nullptr || tail_address == nullptr || queue->depth <= 1U)
        return NDS_OPERATION_INVALID_ARGUMENT;
    NdsAivCacheSync(reinterpret_cast<__gm__ uint8_t *>(head_address), sizeof(uint32_t));
    NdsAivCacheSync(reinterpret_cast<__gm__ uint8_t *>(tail_address), sizeof(uint32_t));
    const uint32_t head = *head_address;
    const uint32_t used = head - *tail_address;
    const uint32_t queue_available = used < queue->depth - 1U ? queue->depth - 1U - used : 0U;
    if (queue_available == 0U || state->send_credits == 0U) {
        uint32_t reclaimed = 0U;
        const uint32_t reclaim_status = ReclaimTransportCq(qp, state, 1U, &reclaimed);
        if (reclaim_status != NDS_OPERATION_SUCCESS)
            return reclaim_status;
    }
    NdsAivCacheSync(reinterpret_cast<__gm__ uint8_t *>(head_address), sizeof(uint32_t));
    NdsAivCacheSync(reinterpret_cast<__gm__ uint8_t *>(tail_address), sizeof(uint32_t));
    const uint32_t current_head = *head_address;
    const uint32_t current_used = current_head - *tail_address;
    const uint32_t current_queue_available = current_used < queue->depth - 1U ? queue->depth - 1U - current_used : 0U;
    const uint32_t posted = wr_count < current_queue_available ? wr_count : current_queue_available;
    const uint32_t limited_posted = posted < state->send_credits ? posted : state->send_credits;
    for (uint32_t index = 0U; index < wr_count; ++index) {
        const NdsSendWr local = LoadSendWr(&wrs[index]);
        if (!ValidSendWr(&local)) {
            SetBadWrAddress(bad_wr, reinterpret_cast<uint64_t>(&wrs[index]));
            return NDS_OPERATION_INVALID_ARGUMENT;
        }
    }
    if (limited_posted == 0U) {
        SetBadWrAddress(bad_wr, reinterpret_cast<uint64_t>(wrs));
        return NDS_OPERATION_QUEUE_FULL;
    }

    NdsTransportQpState next = LoadState(state);
    uint32_t signal_count = 0U;
    for (uint32_t index = 0U; index < limited_posted; ++index) {
        NdsSendWr local = LoadSendWr(&wrs[index]);
        const uint32_t signaled = next.unsignaled_count + 1U >= next.signal_interval ? 1U : 0U;
        local.flags = signaled != 0U ? NDS_SEND_SIGNALED : 0U;
        PopulateDeviceSendWqe(queue, current_head + index, &local);
        if (next.send_credits == 0U)
            return NDS_OPERATION_QUEUE_FULL;
        --next.send_credits;
        next.unsignaled_count = signaled != 0U ? 0U : next.unsignaled_count + 1U;
        signal_count += signaled;
    }
    RingDeviceSendDoorbell(qp->service_level, queue, current_head + limited_posted, scratch);
    StoreState(state, next);
    if (signal_count != 0U) {
        const uint32_t target = state->send_credits + signal_count * state->signal_interval;
        const uint32_t wait_status = WaitForSendCompletion(qp, state, target);
        if (wait_status != NDS_OPERATION_SUCCESS)
            return wait_status;
    }
    if (limited_posted != wr_count) {
        SetBadWrAddress(bad_wr, reinterpret_cast<uint64_t>(&wrs[limited_posted]));
        return NDS_OPERATION_QUEUE_FULL;
    }
    SetBadWrAddress(bad_wr, 0U);
    return NDS_OPERATION_SUCCESS;
}

}  // namespace

NDS_AIV_DEVICE_API_LINKAGE __aicore__ void nds_aiv_rdma_send(__gm__ const NdsTransportDescriptor *transport,
                                                             uint32_t queue_index, const NdsSendWr *wr,
                                                             __gm__ int32_t *return_value, TBuf<> *scratch) {
    if (return_value == nullptr)
        return;
    NdsAivSetReturnValue(return_value, PostTransportSend(transport, queue_index, wr, return_value, scratch));
}

NDS_AIV_DEVICE_API_LINKAGE __aicore__ void nds_aiv_rdma_send_batch(__gm__ const NdsTransportDescriptor *transport,
                                                                   uint32_t queue_index, __gm__ const NdsSendWr *wrs,
                                                                   uint32_t wr_count, __gm__ int32_t *return_value,
                                                                   __gm__ uint64_t *bad_wr, TBuf<> *scratch) {
    if (return_value == nullptr)
        return;
    if (bad_wr != nullptr)
        SetBadWrAddress(bad_wr, 0U);
    NdsAivSetReturnValue(return_value,
                         PostTransportSendBatch(transport, queue_index, wrs, wr_count, return_value, bad_wr, scratch));
}

NDS_AIV_DEVICE_API_LINKAGE __aicore__ void nds_aiv_rdma_recv(__gm__ const NdsTransportDescriptor *transport,
                                                             uint32_t queue_index, const NdsRecvWr *wr,
                                                             __gm__ int32_t *return_value) {
    if (return_value == nullptr)
        return;
    if (transport == nullptr || wr == nullptr) {
        NdsAivSetReturnValue(return_value, NDS_OPERATION_INVALID_ARGUMENT);
        return;
    }
    __gm__ const NdsQpDescriptor *qp = nds_transport_qp_global(transport, queue_index);
    __gm__ NdsTransportQpState *state = nds_transport_qp_state_global(transport, queue_index);
    if (qp == nullptr || state == nullptr) {
        NdsAivSetReturnValue(return_value, NDS_OPERATION_INVALID_ARGUMENT);
        return;
    }
    const uint32_t reclaim_status = ReclaimIfNeeded(qp, state, 0U);
    if (reclaim_status != NDS_OPERATION_SUCCESS || state->receive_credits == 0U) {
        NdsAivSetReturnValue(return_value,
                             reclaim_status != NDS_OPERATION_SUCCESS ? reclaim_status : NDS_OPERATION_QUEUE_FULL);
        return;
    }
    const NdsQpDescriptor local_qp = LoadQp(qp);
    nds_aiv_post_recv(&local_qp, wr, return_value);
    if (*return_value == 0 && nds_transport_record_receive(state) == 0U)
        NdsAivSetReturnValue(return_value, NDS_OPERATION_QUEUE_FULL);
}

NDS_AIV_DEVICE_API_LINKAGE __aicore__ void nds_aiv_rdma_read(__gm__ const NdsTransportDescriptor *transport,
                                                             uint32_t queue_index, const NdsSendWr *wr,
                                                             __gm__ int32_t *return_value, TBuf<> *scratch) {
    if (return_value == nullptr)
        return;
    NdsAivSetReturnValue(return_value, PostTransportSend(transport, queue_index, wr, return_value, scratch));
}

NDS_AIV_DEVICE_API_LINKAGE __aicore__ void nds_aiv_rdma_write(__gm__ const NdsTransportDescriptor *transport,
                                                              uint32_t queue_index, const NdsSendWr *wr,
                                                              __gm__ int32_t *return_value, TBuf<> *scratch) {
    if (return_value == nullptr)
        return;
    NdsAivSetReturnValue(return_value, PostTransportSend(transport, queue_index, wr, return_value, scratch));
}
