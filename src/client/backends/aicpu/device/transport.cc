#include "api.h"
#include "internal.h"

namespace {
constexpr uint32_t kTransportPollBatch = NDS_MAX_COMPLETIONS;

uint32_t reclaim(const NdsQpDescriptor *qp, NdsTransportQpState *state, bool send_cq) {
    if (qp == nullptr || state == nullptr)
        return NDS_OPERATION_INVALID_ARGUMENT;
    NdsWc completions[kTransportPollBatch]{};
    int32_t poll_result = -static_cast<int32_t>(NDS_OPERATION_INVALID_ARGUMENT);
    const uint32_t provider_result =
        nds_aicpu_poll_cq(qp, send_cq ? 1U : 0U, kTransportPollBatch, completions, &poll_result);
    if (provider_result != kNdsAicpuSuccess || poll_result < 0)
        return poll_result < 0 ? static_cast<uint32_t>(-poll_result)
                               : static_cast<uint32_t>(NDS_OPERATION_PROVIDER_FAILED);
    const uint32_t count = static_cast<uint32_t>(poll_result);
    for (uint32_t index = 0U; index < count; ++index) {
        if (completions[index].status != NDS_WC_SUCCESS)
            return NDS_OPERATION_PROVIDER_FAILED;
    }
    if (send_cq)
        nds_transport_reclaim_send(state, count);
    else
        nds_transport_reclaim_receive(state, count);
    return NDS_OPERATION_SUCCESS;
}

uint32_t reclaim_if_needed(const NdsQpDescriptor *qp, NdsTransportQpState *state, bool send_cq) {
    const uint32_t credits = send_cq ? state->send_credits : state->receive_credits;
    if (credits == 0U)
        return reclaim(qp, state, send_cq);
    return static_cast<uint32_t>(NDS_OPERATION_SUCCESS);
}

uint32_t wait_for_send_completion(const NdsQpDescriptor *qp, NdsTransportQpState *state, uint32_t target_credits) {
    while (state->send_credits < target_credits) {
        const uint32_t status = reclaim(qp, state, true);
        if (status != NDS_OPERATION_SUCCESS)
            return status;
    }
    return NDS_OPERATION_SUCCESS;
}

uint32_t post_send(const NdsTransportDescriptor *transport, uint32_t queue_index, const NdsSendWr *wr,
                   int32_t *return_value) {
    if (transport == nullptr || wr == nullptr || return_value == nullptr)
        return kNdsAicpuInvalidArgument;
    const NdsQpDescriptor *qp = nds_transport_qp(transport, queue_index);
    NdsTransportQpState *state = nds_transport_qp_state(transport, queue_index);
    if (qp == nullptr || state == nullptr || state->signal_interval == 0U) {
        NdsAicpuSetReturnValue(return_value, NDS_OPERATION_INVALID_ARGUMENT);
        return kNdsAicpuSuccess;
    }
    uint32_t status = reclaim_if_needed(qp, state, true);
    if (status != NDS_OPERATION_SUCCESS) {
        NdsAicpuSetReturnValue(return_value, status);
        return kNdsAicpuSuccess;
    }
    if (state->send_credits == 0U) {
        NdsAicpuSetReturnValue(return_value, NDS_OPERATION_QUEUE_FULL);
        return kNdsAicpuSuccess;
    }

    NdsTransportQpState next = *state;
    const uint32_t signaled = nds_transport_should_signal(&next);
    NdsSendWr scheduled = *wr;
    scheduled.flags = signaled != 0U ? static_cast<uint32_t>(NDS_SEND_SIGNALED) : 0U;
    int32_t post_result = -static_cast<int32_t>(NDS_OPERATION_PROVIDER_FAILED);
    if (nds_aicpu_post_send(qp, &scheduled, &post_result) != kNdsAicpuSuccess || post_result != 0) {
        NdsAicpuSetReturnValue(return_value, post_result < 0 ? static_cast<uint32_t>(-post_result)
                                                             : static_cast<uint32_t>(NDS_OPERATION_PROVIDER_FAILED));
        return kNdsAicpuSuccess;
    }
    if (nds_transport_record_send(&next, signaled) == 0U) {
        NdsAicpuSetReturnValue(return_value, NDS_OPERATION_QUEUE_FULL);
        return kNdsAicpuSuccess;
    }
    *state = next;
    if (signaled != 0U)
        status = wait_for_send_completion(qp, state, state->send_credits + state->signal_interval);
    else
        status = NDS_OPERATION_SUCCESS;
    NdsAicpuSetReturnValue(return_value, status);
    return kNdsAicpuSuccess;
}
}  // namespace

extern "C" uint32_t nds_aicpu_rdma_send(const NdsTransportDescriptor *transport, uint32_t queue_index,
                                        const NdsSendWr *wr, int32_t *return_value) {
    return post_send(transport, queue_index, wr, return_value);
}

extern "C" uint32_t nds_aicpu_rdma_recv(const NdsTransportDescriptor *transport, uint32_t queue_index,
                                        const NdsRecvWr *wr, int32_t *return_value) {
    if (transport == nullptr || wr == nullptr || return_value == nullptr)
        return kNdsAicpuInvalidArgument;
    const NdsQpDescriptor *qp = nds_transport_qp(transport, queue_index);
    NdsTransportQpState *state = nds_transport_qp_state(transport, queue_index);
    if (qp == nullptr || state == nullptr) {
        NdsAicpuSetReturnValue(return_value, NDS_OPERATION_INVALID_ARGUMENT);
        return kNdsAicpuSuccess;
    }
    uint32_t status = reclaim_if_needed(qp, state, false);
    if (status == NDS_OPERATION_SUCCESS && state->receive_credits == 0U)
        status = NDS_OPERATION_QUEUE_FULL;
    if (status == NDS_OPERATION_SUCCESS) {
        int32_t post_result = -static_cast<int32_t>(NDS_OPERATION_PROVIDER_FAILED);
        if (nds_aicpu_post_recv(qp, wr, &post_result) != kNdsAicpuSuccess || post_result != 0)
            status = post_result < 0 ? static_cast<uint32_t>(-post_result)
                                     : static_cast<uint32_t>(NDS_OPERATION_PROVIDER_FAILED);
        else if (nds_transport_record_receive(state) == 0U)
            status = NDS_OPERATION_QUEUE_FULL;
    }
    NdsAicpuSetReturnValue(return_value, status);
    return kNdsAicpuSuccess;
}

extern "C" uint32_t nds_aicpu_rdma_read(const NdsTransportDescriptor *transport, uint32_t queue_index,
                                        const NdsSendWr *wr, int32_t *return_value) {
    return post_send(transport, queue_index, wr, return_value);
}

extern "C" uint32_t nds_aicpu_rdma_write(const NdsTransportDescriptor *transport, uint32_t queue_index,
                                         const NdsSendWr *wr, int32_t *return_value) {
    return post_send(transport, queue_index, wr, return_value);
}
