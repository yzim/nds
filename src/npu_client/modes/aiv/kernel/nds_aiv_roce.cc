#include "kernel_operator.h"
#include "nds/aiv_roce_abi.h"

using namespace AscendC;

/* Matches HCOMM's AIV metadata section convention without importing its runtime. */
#define NDS_EXPORT_AIV_META_INFO(kernel_name) \
static const struct FunLevelKType kernel_name##_kernel_type_section __attribute__ \
((used, section (".ascend.meta." #kernel_name))) = {{F_TYPE_KTYPE, sizeof(unsigned int), K_TYPE_AIV}}

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

__aicore__ inline void CacheWriteThrough(__gm__ uint8_t *address, uint64_t length)
{
    __gm__ uint8_t *start = (__gm__ uint8_t *)((uint64_t)address / CACHE_LINE_SIZE * CACHE_LINE_SIZE);
    __gm__ uint8_t *end = (__gm__ uint8_t *)(((uint64_t)address + length) / CACHE_LINE_SIZE * CACHE_LINE_SIZE);
    GlobalTensor<uint8_t> global;
    global.SetGlobalBuffer(start);
    for (uint32_t offset = 0U; offset <= end - start; offset += CACHE_LINE_SIZE) {
        DataCacheCleanAndInvalid<uint8_t, CacheLine::SINGLE_CACHE_LINE, DcciDst::CACHELINE_OUT>(global[offset]);
    }
}

__aicore__ inline void StoreU64WithDma(TBuf<> &scratch, __gm__ uint64_t *destination, uint64_t value)
{
    LocalTensor<uint64_t> local = scratch.GetWithOffset<uint64_t>(1U, 0U);
    local.SetValue(0U, value);
    GlobalTensor<uint64_t> global;
    global.SetGlobalBuffer(destination);
    DataCopyExtParams params{1U, sizeof(uint64_t), 0U, 0U, 0U};
    DataCopyPad(global, local, params);
}
} // namespace

extern "C" __global__ __aicore__ void NdsAivRdmaWrite(GM_ADDR request_address)
{
    __gm__ const nds_aiv_rdma_write_request *request =
        reinterpret_cast<__gm__ const nds_aiv_rdma_write_request *>(request_address);
    __gm__ const nds_aiv_sq_descriptor *queue = &request->send_queue;
    __gm__ uint32_t *head_address = reinterpret_cast<__gm__ uint32_t *>(queue->head_address);
    __gm__ uint32_t *tail_address = reinterpret_cast<__gm__ uint32_t *>(queue->tail_address);

    TPipe pipe;
    TBuf<> scratch;
    pipe.InitBuffer(scratch, 64U);
    for (uint32_t index = 0U; index < request->write_count; ++index) {
        CacheWriteThrough(reinterpret_cast<__gm__ uint8_t *>(head_address), sizeof(uint64_t));
        const uint32_t head = *head_address;
        while ((head - *tail_address) >= queue->depth - 1U) {
            CacheWriteThrough(reinterpret_cast<__gm__ uint8_t *>(tail_address), sizeof(uint64_t));
        }
        __gm__ uint8_t *wqe_address = reinterpret_cast<__gm__ uint8_t *>(
            queue->buffer_address + (uint64_t)queue->wqebb_size * (head % queue->depth));
        const uint32_t owner_bit = (head >> 15U) & 1U;
        __gm__ HnsRoceRcSqWqe *wqe = reinterpret_cast<__gm__ HnsRoceRcSqWqe *>(wqe_address);
        wqe->byte_4 = 3U | (((~owner_bit) << 7U) & (1U << 7U)) | (1U << 8U);
        wqe->message_length = request->length;
        wqe->immediate_data = 0U;
        wqe->sge_count = 1U << 24U;
        wqe->start_sge_index = 0U;
        wqe->remote_key = request->remote_rkey;
        wqe->remote_address = request->remote_address;
        __gm__ HnsRoceSge *sge = reinterpret_cast<__gm__ HnsRoceSge *>(wqe_address + sizeof(HnsRoceRcSqWqe));
        sge->length = request->length;
        sge->local_key = request->local_lkey;
        sge->local_address = request->local_address;
        CacheWriteThrough(wqe_address, sizeof(HnsRoceRcSqWqe) + sizeof(HnsRoceSge));
        PipeBarrier<PIPE_ALL>();
        const uint32_t new_head = head + 1U;
        const uint64_t doorbell = (uint64_t)queue->wqn | ((uint64_t)(new_head & 0xffffU) << 32U) |
                                  ((uint64_t)queue->service_level << 48U);
        StoreU64WithDma(scratch, reinterpret_cast<__gm__ uint64_t *>(queue->doorbell_address), doorbell);
        PipeBarrier<PIPE_ALL>();
        StoreU64WithDma(scratch, reinterpret_cast<__gm__ uint64_t *>(head_address), new_head);
        PipeBarrier<PIPE_ALL>();
    }
}
NDS_EXPORT_AIV_META_INFO(NdsAivRdmaWrite);
