#include "nds/aiv_device_api.h"
#include "nds/device_hns_codec.h"

using namespace AscendC;

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

__aicore__ inline void CacheSync(__gm__ uint8_t *address, uint64_t length) {
    __gm__ uint8_t *start = (__gm__ uint8_t *)((uint64_t)address / CACHE_LINE_SIZE * CACHE_LINE_SIZE);
    __gm__ uint8_t *end = (__gm__ uint8_t *)(((uint64_t)address + length) / CACHE_LINE_SIZE * CACHE_LINE_SIZE);
    GlobalTensor<uint8_t> global;
    global.SetGlobalBuffer(start);
    for (uint32_t offset = 0U; offset <= end - start; offset += CACHE_LINE_SIZE)
        DataCacheCleanAndInvalid<uint8_t, CacheLine::SINGLE_CACHE_LINE, DcciDst::CACHELINE_OUT>(global[offset]);
}

__aicore__ inline void StoreU64(TBuf<> *scratch, uint64_t address, uint64_t value) {
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
    CacheSync(reinterpret_cast<__gm__ uint8_t *>(destination), sizeof(value));
}

__aicore__ inline void SetResult(__gm__ nds_device_operation_result *result, uint32_t status) {
    if (result == nullptr) return;
    result->status = status;
    result->path = NDS_DEVICE_OPERATION_PATH_DIRECT;
    result->provider_result = 0;
    result->reserved = 0U;
    CacheSync(reinterpret_cast<__gm__ uint8_t *>(result), sizeof(*result));
}

__aicore__ inline bool ValidQp(__gm__ const nds_device_qp *qp) {
    return qp != nullptr && qp->abi_version == NDS_DEVICE_QP_ABI_VERSION && qp->size == sizeof(*qp);
}
}  // namespace

NDS_AIV_DEVICE_API_LINKAGE __aicore__ void NdsAivPostSend(__gm__ const nds_device_qp *qp,
                                           const nds_device_send_wr *wr, TBuf<> *scratch,
                                           __gm__ nds_device_operation_result *result) {
    if (result == nullptr) return;
    if (!ValidQp(qp) || wr == nullptr || scratch == nullptr ||
        (wr->opcode != NDS_DEVICE_WR_SEND && wr->opcode != NDS_DEVICE_WR_RDMA_READ &&
         wr->opcode != NDS_DEVICE_WR_RDMA_WRITE)) {
        SetResult(result, NDS_DEVICE_OPERATION_INVALID_ARGUMENT);
        return;
    }
    __gm__ const nds_device_work_queue *queue = &qp->send_queue;
    __gm__ uint32_t *head_address = reinterpret_cast<__gm__ uint32_t *>(queue->head_address);
    __gm__ uint32_t *tail_address = reinterpret_cast<__gm__ uint32_t *>(queue->tail_address);
    CacheSync(reinterpret_cast<__gm__ uint8_t *>(head_address), sizeof(uint32_t));
    const uint32_t head = *head_address;
    CacheSync(reinterpret_cast<__gm__ uint8_t *>(tail_address), sizeof(uint32_t));
    if ((head - *tail_address) >= queue->depth - 1U) {
        SetResult(result, NDS_DEVICE_OPERATION_QUEUE_FULL);
        return;
    }
    __gm__ uint8_t *wqe_address = reinterpret_cast<__gm__ uint8_t *>(
        queue->buffer_address + (uint64_t)queue->entry_size * (head % queue->depth));
    __gm__ HnsRoceRcSqWqe *wqe = reinterpret_cast<__gm__ HnsRoceRcSqWqe *>(wqe_address);
    const uint32_t owner = (head >> 15U) & 1U;
    const uint32_t hns_opcode = NDS_HNS_SQ_OPCODE_FROM_DEVICE(wr->opcode);
    wqe->byte_4 = hns_opcode | (((~owner) << 7U) & (1U << 7U)) | (1U << 8U);
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
    __gm__ uint64_t *wr_id = &reinterpret_cast<__gm__ uint64_t *>(queue->wr_id_address)[head % queue->depth];
    *wr_id = wr->wr_id;
    CacheSync(reinterpret_cast<__gm__ uint8_t *>(wr_id), sizeof(*wr_id));
    CacheSync(wqe_address, sizeof(HnsRoceRcSqWqe) + sizeof(HnsRoceSge));
    PipeBarrier<PIPE_ALL>();
    const uint32_t next = head + 1U;
    const uint64_t doorbell = (uint64_t)queue->number | ((uint64_t)(next & 0xffffU) << 32U) |
                              ((uint64_t)qp->service_level << 48U);
    StoreU64(scratch, queue->doorbell_address, doorbell);
    StoreU32(queue->head_address, next);
    SetResult(result, NDS_DEVICE_OPERATION_SUCCESS);
}

NDS_AIV_DEVICE_API_LINKAGE __aicore__ void NdsAivPostRecv(__gm__ const nds_device_qp *qp,
                                           const nds_device_recv_wr *wr, TBuf<> *scratch,
                                           __gm__ nds_device_operation_result *result) {
    (void)scratch;
    if (result == nullptr) return;
    if (!ValidQp(qp) || wr == nullptr) {
        SetResult(result, NDS_DEVICE_OPERATION_INVALID_ARGUMENT);
        return;
    }
    __gm__ const nds_device_work_queue *queue = &qp->receive_queue;
    __gm__ uint32_t *head_address = reinterpret_cast<__gm__ uint32_t *>(queue->head_address);
    __gm__ uint32_t *tail_address = reinterpret_cast<__gm__ uint32_t *>(queue->tail_address);
    CacheSync(reinterpret_cast<__gm__ uint8_t *>(head_address), sizeof(uint32_t));
    CacheSync(reinterpret_cast<__gm__ uint8_t *>(tail_address), sizeof(uint32_t));
    const uint32_t head = *head_address;
    if ((head - *tail_address) >= queue->depth) {
        SetResult(result, NDS_DEVICE_OPERATION_QUEUE_FULL);
        return;
    }
    __gm__ HnsRoceSge *sge = reinterpret_cast<__gm__ HnsRoceSge *>(
        queue->buffer_address + (uint64_t)queue->entry_size * (head % queue->depth));
    sge->length = wr->local.length;
    sge->local_key = wr->local.local_key;
    sge->local_address = wr->local.address;
    __gm__ uint64_t *wr_id = &reinterpret_cast<__gm__ uint64_t *>(queue->wr_id_address)[head % queue->depth];
    *wr_id = wr->wr_id;
    CacheSync(reinterpret_cast<__gm__ uint8_t *>(wr_id), sizeof(*wr_id));
    CacheSync(reinterpret_cast<__gm__ uint8_t *>(sge), sizeof(HnsRoceSge));
    PipeBarrier<PIPE_ALL>();
    const uint32_t next = head + 1U;
    StoreU32(queue->doorbell_address, next & 0xffffU);
    StoreU32(queue->head_address, next);
    SetResult(result, NDS_DEVICE_OPERATION_SUCCESS);
}

NDS_AIV_DEVICE_API_LINKAGE __aicore__ void NdsAivPollCq(__gm__ const nds_device_qp *qp,
                                         __gm__ const nds_device_poll_cq_request *request, TBuf<> *scratch,
                                         __gm__ nds_device_operation_result *result) {
    (void)scratch;
    if (result == nullptr) return;
    if (!ValidQp(qp) || request == nullptr || request->completion_output_address == 0U) {
        SetResult(result, NDS_DEVICE_OPERATION_INVALID_ARGUMENT);
        return;
    }
    const bool receive = request->queue_kind == NDS_DEVICE_RECEIVE_QUEUE;
    __gm__ const nds_device_completion_queue *cq = receive ? &qp->receive_cq : &qp->send_cq;
    __gm__ const nds_device_work_queue *wq = receive ? &qp->receive_queue : &qp->send_queue;
    __gm__ uint32_t *consumer_address = reinterpret_cast<__gm__ uint32_t *>(cq->consumer_address);
    __gm__ uint32_t *tail_address = reinterpret_cast<__gm__ uint32_t *>(wq->tail_address);
    CacheSync(reinterpret_cast<__gm__ uint8_t *>(consumer_address), sizeof(uint32_t));
    uint32_t consumer = *consumer_address;
    CacheSync(reinterpret_cast<__gm__ uint8_t *>(tail_address), sizeof(uint32_t));
    uint32_t tail = *tail_address;
    __gm__ nds_device_completion_output *output = reinterpret_cast<__gm__ nds_device_completion_output *>(
        request->completion_output_address);
    uint32_t count = 0U;
    const uint32_t limit = request->max_completions < NDS_DEVICE_MAX_COMPLETIONS ?
                               request->max_completions : NDS_DEVICE_MAX_COMPLETIONS;
    while (count < limit) {
        __gm__ nds_hns_cqe *cqe = reinterpret_cast<__gm__ nds_hns_cqe *>(
            cq->buffer_address + (uint64_t)cq->entry_size * (consumer % cq->depth));
        CacheSync(reinterpret_cast<__gm__ uint8_t *>(cqe), sizeof(nds_hns_cqe));
        const uint32_t owner = (cqe->byte_4 >> 7U) & 1U;
        if ((owner ^ !!(consumer & cq->depth)) == 0U) break;
        const uint32_t wqe_index = cqe->byte_4 >> 16U;
        if (!receive) tail += (wqe_index - tail) & (wq->depth - 1U);
        __gm__ nds_device_completion *completion = &output->entries[count++];
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
    output->count = count;
    if (count != 0U) {
        StoreU32(cq->consumer_address, consumer);
        StoreU32(wq->tail_address, tail);
        StoreU32(cq->doorbell_address, consumer & 0x00ffffffU);
    }
    SetResult(result, NDS_DEVICE_OPERATION_SUCCESS);
}

NDS_AIV_DEVICE_API_LINKAGE __aicore__ void NdsAivRdmaSend(__gm__ const nds_device_connection *connection,
                                           __gm__ const nds_device_transfer *transfer, TBuf<> *scratch,
                                           __gm__ nds_device_operation_result *result) {
    if (result == nullptr) return;
    if (connection == nullptr || transfer == nullptr ||
        (connection->abi_version != NDS_DEVICE_CONNECTION_ABI_VERSION ||
         connection->size != sizeof(*connection))) {
        SetResult(result, NDS_DEVICE_OPERATION_INVALID_ARGUMENT);
        return;
    }
    nds_device_send_wr wr{};
    wr.wr_id = transfer->wr_id;
    wr.opcode = NDS_DEVICE_WR_SEND;
    wr.flags = NDS_DEVICE_SEND_SIGNALED;
    wr.local.address = transfer->local.address;
    wr.local.length = transfer->local.length;
    wr.local.local_key = transfer->local.local_key;
    NdsAivPostSend(&connection->qp, &wr, scratch, result);
}

NDS_AIV_DEVICE_API_LINKAGE __aicore__ void NdsAivRdmaRecv(__gm__ const nds_device_connection *connection,
                                           __gm__ const nds_device_transfer *transfer, TBuf<> *scratch,
                                           __gm__ nds_device_operation_result *result) {
    if (result == nullptr) return;
    if (connection == nullptr || transfer == nullptr ||
        (connection->abi_version != NDS_DEVICE_CONNECTION_ABI_VERSION ||
         connection->size != sizeof(*connection))) {
        SetResult(result, NDS_DEVICE_OPERATION_INVALID_ARGUMENT);
        return;
    }
    nds_device_recv_wr wr{};
    wr.wr_id = transfer->wr_id;
    wr.local.address = transfer->local.address;
    wr.local.length = transfer->local.length;
    wr.local.local_key = transfer->local.local_key;
    NdsAivPostRecv(&connection->qp, &wr, scratch, result);
}

NDS_AIV_DEVICE_API_LINKAGE __aicore__ void NdsAivRdmaRead(__gm__ const nds_device_connection *connection,
                                           __gm__ const nds_device_transfer *transfer, TBuf<> *scratch,
                                           __gm__ nds_device_operation_result *result) {
    if (result == nullptr) return;
    if (connection == nullptr || transfer == nullptr ||
        (connection->abi_version != NDS_DEVICE_CONNECTION_ABI_VERSION ||
         connection->size != sizeof(*connection))) {
        SetResult(result, NDS_DEVICE_OPERATION_INVALID_ARGUMENT);
        return;
    }
    nds_device_send_wr wr{};
    wr.wr_id = transfer->wr_id;
    wr.opcode = NDS_DEVICE_WR_RDMA_READ;
    wr.flags = NDS_DEVICE_SEND_SIGNALED;
    wr.local.address = transfer->local.address;
    wr.local.length = transfer->local.length;
    wr.local.local_key = transfer->local.local_key;
    wr.remote_address = transfer->remote_address;
    wr.remote_key = transfer->remote_key;
    NdsAivPostSend(&connection->qp, &wr, scratch, result);
}

NDS_AIV_DEVICE_API_LINKAGE __aicore__ void NdsAivRdmaWrite(__gm__ const nds_device_connection *connection,
                                            __gm__ const nds_device_transfer *transfer, TBuf<> *scratch,
                                            __gm__ nds_device_operation_result *result) {
    if (result == nullptr) return;
    if (connection == nullptr || transfer == nullptr ||
        (connection->abi_version != NDS_DEVICE_CONNECTION_ABI_VERSION ||
         connection->size != sizeof(*connection))) {
        SetResult(result, NDS_DEVICE_OPERATION_INVALID_ARGUMENT);
        return;
    }
    nds_device_send_wr wr{};
    wr.wr_id = transfer->wr_id;
    wr.opcode = NDS_DEVICE_WR_RDMA_WRITE;
    wr.flags = NDS_DEVICE_SEND_SIGNALED;
    wr.local.address = transfer->local.address;
    wr.local.length = transfer->local.length;
    wr.local.local_key = transfer->local.local_key;
    wr.remote_address = transfer->remote_address;
    wr.remote_key = transfer->remote_key;
    NdsAivPostSend(&connection->qp, &wr, scratch, result);
}
