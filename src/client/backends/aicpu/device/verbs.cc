#include "api.h"
#include "../../common/hns_hw.h"
#include "internal.h"
#include "hns.h"

namespace {
int32_t provider_opcode(uint32_t opcode) {
    if (opcode == NDS_DEVICE_WR_SEND)
        return NDS_HNS_WR_SEND;
    if (opcode == NDS_DEVICE_WR_RDMA_READ)
        return NDS_HNS_WR_RDMA_READ;
    if (opcode == NDS_DEVICE_WR_RDMA_WRITE)
        return NDS_HNS_WR_RDMA_WRITE;
    return -1;
}
}  // namespace

extern "C" uint32_t nds_aicpu_post_send(const NdsDeviceQp *qp, const NdsDeviceSendWr *wr, int32_t *return_value) {
    if (return_value == nullptr)
        return kNdsAicpuInvalidArgument;
    if (!NdsAicpuValidQp(qp) || wr == nullptr) {
        NdsAicpuSetReturnValue(return_value, NDS_DEVICE_OPERATION_INVALID_ARGUMENT);
        return kNdsAicpuSuccess;
    }
    const int32_t opcode = provider_opcode(wr->opcode);
    if (opcode < 0) {
        NdsAicpuSetReturnValue(return_value, NDS_DEVICE_OPERATION_UNSUPPORTED);
        return kNdsAicpuSuccess;
    }
    if (qp->qp_mode != NDS_DEVICE_QP_MODE_NORMAL) {
        NdsAicpuSetReturnValue(return_value, NDS_DEVICE_OPERATION_UNSUPPORTED);
        return kNdsAicpuSuccess;
    }
    auto post = reinterpret_cast<NdsHnsExpPostSendFn>(NdsAicpuResolveSymbol("ibv_exp_post_send"));
    if (post == nullptr) {
        NdsAicpuSetReturnValue(return_value, NDS_DEVICE_OPERATION_SYMBOL_UNAVAILABLE);
        return kNdsAicpuSuccess;
    }
    if (qp->send_queue.wr_id_address == 0U || qp->send_queue.head_address == 0U || qp->send_queue.depth == 0U) {
        NdsAicpuSetReturnValue(return_value, NDS_DEVICE_OPERATION_INVALID_ARGUMENT);
        return kNdsAicpuSuccess;
    }
    const uint32_t producer = *reinterpret_cast<uint32_t *>(qp->send_queue.head_address);
    reinterpret_cast<uint64_t *>(qp->send_queue.wr_id_address)[producer % qp->send_queue.depth] = wr->wr_id;
    NdsAicpuBarrier();
    NdsHnsSge sge{wr->local.address, wr->local.length, wr->local.local_key};
    NdsHnsSendWr provider_wr{};
    provider_wr.wr_id = wr->wr_id;
    provider_wr.sg_list = &sge;
    provider_wr.num_sge = 1;
    provider_wr.opcode = opcode;
    provider_wr.send_flags =
        (wr->flags & NDS_DEVICE_SEND_SIGNALED) != 0U ? static_cast<uint32_t>(NDS_HNS_SEND_SIGNALED) : 0U;
    provider_wr.rdma.remote_addr = wr->remote_address;
    provider_wr.rdma.rkey = wr->remote_key;
    NdsHnsSendWr *bad = nullptr;
    NdsHnsPostSendResponse response{};
    const int provider_result = post(reinterpret_cast<void *>(qp->provider_qp_address), &provider_wr, &bad, &response);
    NdsAicpuSetReturnValue(return_value,
                           provider_result == 0 ? NDS_DEVICE_OPERATION_SUCCESS : NDS_DEVICE_OPERATION_PROVIDER_FAILED);
    return kNdsAicpuSuccess;
}

extern "C" uint32_t nds_aicpu_post_recv(const NdsDeviceQp *qp, const NdsDeviceRecvWr *wr, int32_t *return_value) {
    if (return_value == nullptr)
        return kNdsAicpuInvalidArgument;
    if (!NdsAicpuValidQp(qp) || wr == nullptr) {
        NdsAicpuSetReturnValue(return_value, NDS_DEVICE_OPERATION_INVALID_ARGUMENT);
        return kNdsAicpuSuccess;
    }
    const NdsDeviceWorkQueue &queue = qp->receive_queue;
    if (queue.buffer_address == 0U || queue.head_address == 0U || queue.tail_address == 0U ||
        queue.doorbell_address == 0U || queue.wr_id_address == 0U || queue.depth == 0U ||
        queue.entry_size < sizeof(NdsHnsHwWqeDataSeg)) {
        NdsAicpuSetReturnValue(return_value, NDS_DEVICE_OPERATION_INVALID_ARGUMENT);
        return kNdsAicpuSuccess;
    }
    NdsAicpuBarrier();
    const uint32_t producer = *reinterpret_cast<uint32_t *>(queue.head_address);
    const uint32_t consumer = *reinterpret_cast<uint32_t *>(queue.tail_address);
    if (producer - consumer >= queue.depth) {
        NdsAicpuSetReturnValue(return_value, NDS_DEVICE_OPERATION_QUEUE_FULL);
        return kNdsAicpuSuccess;
    }
    auto *segment = reinterpret_cast<NdsHnsHwWqeDataSeg *>(
        queue.buffer_address + static_cast<uint64_t>(queue.entry_size) * (producer % queue.depth));
    nds_hns_hw_encode_wqe_data_seg(segment, wr->local.address, wr->local.length, wr->local.local_key);
    reinterpret_cast<uint64_t *>(queue.wr_id_address)[producer % queue.depth] = wr->wr_id;
    NdsAicpuBarrier();
    *reinterpret_cast<uint32_t *>(queue.doorbell_address) = (producer + 1U) & 0xffffU;
    *reinterpret_cast<uint32_t *>(queue.head_address) = producer + 1U;
    NdsAicpuBarrier();
    NdsAicpuSetReturnValue(return_value, NDS_DEVICE_OPERATION_SUCCESS);
    return kNdsAicpuSuccess;
}

extern "C" uint32_t nds_aicpu_poll_cq(const NdsDeviceQp *qp, uint32_t is_send_cq_value, uint32_t max_completions,
                                      NdsDeviceWc *wc, int32_t *return_value) {
    if (return_value == nullptr)
        return kNdsAicpuInvalidArgument;
    if (!NdsAicpuValidQp(qp) || is_send_cq_value > 1U || wc == nullptr || max_completions == 0U) {
        NdsAicpuSetReturnValue(return_value, NDS_DEVICE_OPERATION_INVALID_ARGUMENT);
        return kNdsAicpuSuccess;
    }
    const bool is_send_cq = is_send_cq_value != 0U;
    const uint32_t limit = max_completions < NDS_DEVICE_MAX_COMPLETIONS ? max_completions : NDS_DEVICE_MAX_COMPLETIONS;
    const NdsDeviceCq *cq = is_send_cq ? &qp->send_cq : &qp->receive_cq;
    const NdsDeviceWorkQueue *wq = is_send_cq ? &qp->send_queue : &qp->receive_queue;
    if (cq->buffer_address == 0U || cq->consumer_address == 0U || cq->depth == 0U ||
        cq->entry_size < sizeof(NdsHnsHwCqe) || wq->tail_address == 0U || wq->wr_id_address == 0U) {
        NdsAicpuSetReturnValue(return_value, NDS_DEVICE_OPERATION_INVALID_ARGUMENT);
        return kNdsAicpuSuccess;
    }
    NdsAicpuBarrier();
    uint32_t consumer = *reinterpret_cast<uint32_t *>(cq->consumer_address);
    uint32_t tail = *reinterpret_cast<uint32_t *>(wq->tail_address);
    uint32_t count = 0U;
    while (count < limit) {
        const auto *cqe = reinterpret_cast<const NdsHnsHwCqe *>(
            cq->buffer_address + static_cast<uint64_t>(cq->entry_size) * (consumer % cq->depth));
        NdsAicpuBarrier();
        if (!nds_hns_hw_cqe_is_ready(cqe, consumer, cq->depth))
            break;
        if (is_send_cq)
            tail = nds_hns_hw_send_tail_for_cqe(tail, wq->depth, cqe);
        nds_hns_hw_decode_cqe(cqe, reinterpret_cast<uint64_t *>(wq->wr_id_address)[tail % wq->depth], &wc[count]);
        ++consumer;
        ++tail;
        ++count;
    }
    if (count != 0U) {
        *reinterpret_cast<uint32_t *>(cq->consumer_address) = consumer;
        *reinterpret_cast<uint32_t *>(wq->tail_address) = tail;
        if (cq->doorbell_address != 0U)
            *reinterpret_cast<uint32_t *>(cq->doorbell_address) = consumer & 0x00ffffffU;
        NdsAicpuBarrier();
    }
    *return_value = static_cast<int32_t>(count);
    return kNdsAicpuSuccess;
}
