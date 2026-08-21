#include "api.h"
#include "internal.h"
#include "hns.h"
#include "hns_hw.h"

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

extern "C" uint32_t NdsAicpuPostSendImpl(const NdsDeviceQp *qp, const NdsDeviceSendWr *wr,
                                         NdsDeviceOperationResult *result) {
    if (!NdsAicpuValidQp(qp) || wr == nullptr || result == nullptr)
        return kNdsAicpuInvalidArgument;
    const int32_t opcode = provider_opcode(wr->opcode);
    if (opcode < 0) {
        NdsAicpuSetResult(result, NDS_DEVICE_OPERATION_UNSUPPORTED, NDS_DEVICE_OPERATION_PATH_NONE, 0);
        return kNdsAicpuSuccess;
    }
    if (qp->qp_mode != NDS_DEVICE_QP_MODE_NORMAL) {
        NdsAicpuSetResult(result, NDS_DEVICE_OPERATION_UNSUPPORTED, NDS_DEVICE_OPERATION_PATH_NONE, 0);
        return kNdsAicpuSuccess;
    }
    auto post = reinterpret_cast<NdsHnsExpPostSendFn>(NdsAicpuResolveSymbol("ibv_exp_post_send"));
    if (post == nullptr) {
        NdsAicpuSetResult(result, NDS_DEVICE_OPERATION_SYMBOL_UNAVAILABLE, NDS_DEVICE_OPERATION_PATH_PROVIDER, 0);
        return kNdsAicpuSuccess;
    }
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
    NdsAicpuSetResult(result,
                      provider_result == 0 ? NDS_DEVICE_OPERATION_SUCCESS : NDS_DEVICE_OPERATION_PROVIDER_FAILED,
                      NDS_DEVICE_OPERATION_PATH_PROVIDER, provider_result);
    return kNdsAicpuSuccess;
}

extern "C" uint32_t NdsAicpuPostRecvImpl(const NdsDeviceQp *qp, const NdsDeviceRecvWr *wr,
                                         NdsDeviceOperationResult *result) {
    if (!NdsAicpuValidQp(qp) || wr == nullptr || result == nullptr)
        return kNdsAicpuInvalidArgument;
    auto post = reinterpret_cast<NdsHnsPostRecvFn>(NdsAicpuResolveSymbol("ibv_post_recv"));
    if (post != nullptr) {
        NdsHnsSge sge{wr->local.address, wr->local.length, wr->local.local_key};
        NdsHnsRecvWr provider_wr{wr->wr_id, nullptr, &sge, 1, 0U};
        NdsHnsRecvWr *bad = nullptr;
        const int provider_result = post(reinterpret_cast<void *>(qp->provider_qp_address), &provider_wr, &bad);
        NdsAicpuSetResult(result,
                          provider_result == 0 ? NDS_DEVICE_OPERATION_SUCCESS : NDS_DEVICE_OPERATION_PROVIDER_FAILED,
                          NDS_DEVICE_OPERATION_PATH_PROVIDER, provider_result);
        return kNdsAicpuSuccess;
    }
    const NdsDeviceWorkQueue &queue = qp->receive_queue;
    auto *head = reinterpret_cast<uint32_t *>(queue.head_address);
    auto *tail = reinterpret_cast<uint32_t *>(queue.tail_address);
    if (!nds_hns_hw_queue_has_space(*head, *tail, queue.depth, 0U)) {
        NdsAicpuSetResult(result, NDS_DEVICE_OPERATION_QUEUE_FULL, NDS_DEVICE_OPERATION_PATH_DIRECT, 0);
        return kNdsAicpuSuccess;
    }
    const uint32_t index = *head % queue.depth;
    auto *segment = reinterpret_cast<NdsHnsHwWqeDataSeg *>(queue.buffer_address +
                                                                static_cast<uint64_t>(queue.entry_size) * index);
    nds_hns_hw_encode_wqe_data_seg(segment, wr->local.address, wr->local.length, wr->local.local_key);
    reinterpret_cast<uint64_t *>(queue.wr_id_address)[index] = wr->wr_id;
    NdsAicpuBarrier();
    ++*head;
    *reinterpret_cast<uint32_t *>(queue.doorbell_address) = *head & 0xffffU;
    NdsAicpuSetResult(result, NDS_DEVICE_OPERATION_SUCCESS, NDS_DEVICE_OPERATION_PATH_DIRECT, 0);
    return kNdsAicpuSuccess;
}

extern "C" uint32_t NdsAicpuPollCqImpl(const NdsDeviceQp *qp, const NdsDevicePollCqRequest *request,
                                       NdsDeviceOperationResult *result) {
    if (!NdsAicpuValidQp(qp) || request == nullptr || result == nullptr || request->is_send_cq > 1U ||
        request->completion_output_address == 0U || request->max_completions == 0U)
        return kNdsAicpuInvalidArgument;
    const bool is_send_cq = request->is_send_cq != 0U;
    auto poll = reinterpret_cast<NdsHnsPollCqFn>(NdsAicpuResolveSymbol("ibv_poll_cq"));
    const uint32_t limit =
        request->max_completions < NDS_DEVICE_MAX_COMPLETIONS ? request->max_completions : NDS_DEVICE_MAX_COMPLETIONS;
    auto *output = reinterpret_cast<NdsDeviceCompletionOutput *>(request->completion_output_address);
    if (poll != nullptr) {
        NdsHnsWc completions[NDS_DEVICE_MAX_COMPLETIONS]{};
        void *cq = reinterpret_cast<void *>(is_send_cq ? qp->provider_send_cq_address : qp->provider_receive_cq_address);
        const int count = poll(cq, static_cast<int>(limit), completions);
        if (count < 0) {
            NdsAicpuSetResult(result, NDS_DEVICE_OPERATION_PROVIDER_FAILED, NDS_DEVICE_OPERATION_PATH_PROVIDER, count);
            return kNdsAicpuSuccess;
        }
        for (int index = 0; index < count; ++index) {
            output->entries[index] = {
                completions[index].wr_id,      completions[index].status,   completions[index].opcode,
                completions[index].vendor_err, completions[index].byte_len, completions[index].qp_num,
                completions[index].wc_flags,   completions[index].imm_data, 0U};
        }
        output->count = static_cast<uint32_t>(count);
        NdsAicpuSetResult(result, NDS_DEVICE_OPERATION_SUCCESS, NDS_DEVICE_OPERATION_PATH_PROVIDER, 0);
        return kNdsAicpuSuccess;
    }
    const NdsDeviceCompletionQueue &cq = is_send_cq ? qp->send_cq : qp->receive_cq;
    const NdsDeviceWorkQueue &wq = is_send_cq ? qp->send_queue : qp->receive_queue;
    auto *consumer_address = reinterpret_cast<uint32_t *>(cq.consumer_address);
    auto *tail_address = reinterpret_cast<uint32_t *>(wq.tail_address);
    uint32_t consumer = *consumer_address;
    uint32_t tail = *tail_address;
    uint32_t count = 0U;
    while (count < limit) {
        auto *cqe = reinterpret_cast<NdsHnsHwCqe *>(cq.buffer_address +
                                                    static_cast<uint64_t>(cq.entry_size) * (consumer % cq.depth));
        if (!nds_hns_hw_cqe_is_ready(cqe, consumer, cq.depth))
            break;
        if (is_send_cq)
            tail = nds_hns_hw_send_tail_for_cqe(tail, wq.depth, cqe);
        nds_hns_hw_decode_cqe(cqe, reinterpret_cast<uint64_t *>(wq.wr_id_address)[tail % wq.depth],
                           &output->entries[count++]);
        ++consumer;
        ++tail;
    }
    output->count = count;
    if (count != 0U) {
        *consumer_address = consumer;
        *tail_address = tail;
        *reinterpret_cast<uint32_t *>(cq.doorbell_address) = consumer & 0x00ffffffU;
    }
    NdsAicpuSetResult(result, NDS_DEVICE_OPERATION_SUCCESS, NDS_DEVICE_OPERATION_PATH_DIRECT, 0);
    return kNdsAicpuSuccess;
}
