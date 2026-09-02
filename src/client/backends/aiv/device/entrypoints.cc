#define NDS_AIV_DEVICE_API_LINKAGE static
#define NDS_STORAGE_SERDE_INLINE __aicore__ inline

#include "kernel_operator.h"
#include "api.h"

#include "verbs.cc"
#include "transport.cc"
#include "storage.cc"

using namespace AscendC;

namespace {
__aicore__ inline void SetInvalid(__gm__ int32_t *return_value) {
    NdsAivSetReturnValue(return_value, NDS_OPERATION_INVALID_ARGUMENT);
}

}  // namespace

/* Posts one send WR and stores the operation return value. */
extern "C" __global__ __aicore__ void nds_aiv_post_send_kernel(NdsQpDescriptor qp, NdsSendWr wr, GM_ADDR return_value) {
    __gm__ auto *return_value_ptr = reinterpret_cast<__gm__ int32_t *>(return_value);
    TPipe pipe;
    TBuf<> scratch;
    pipe.InitBuffer(scratch, 64U);
    nds_aiv_post_send(&qp, &wr, return_value_ptr, &scratch);
}

/* Posts a contiguous WR array and rings the send doorbell once for its valid prefix. */
extern "C" __global__ __aicore__ void nds_aiv_post_send_batch_kernel(GM_ADDR qp_address, GM_ADDR wrs_address,
                                                                     uint32_t wr_count, GM_ADDR bad_wr_address,
                                                                     GM_ADDR return_value) {
    __gm__ const auto *qp = reinterpret_cast<__gm__ const NdsQpDescriptor *>(qp_address);
    __gm__ const auto *wrs = reinterpret_cast<__gm__ const NdsSendWr *>(wrs_address);
    __gm__ auto *bad_wr = reinterpret_cast<__gm__ uint64_t *>(bad_wr_address);
    __gm__ auto *return_value_ptr = reinterpret_cast<__gm__ int32_t *>(return_value);
    TPipe pipe;
    TBuf<> scratch;
    pipe.InitBuffer(scratch, 64U);
    nds_aiv_post_send_batch(qp, wrs, wr_count, return_value_ptr, bad_wr, &scratch);
}

/* Posts one receive WR and stores the operation return value. */
extern "C" __global__ __aicore__ void nds_aiv_post_recv_kernel(NdsQpDescriptor qp, NdsRecvWr wr, GM_ADDR return_value) {
    __gm__ auto *return_value_ptr = reinterpret_cast<__gm__ int32_t *>(return_value);
    nds_aiv_post_recv(&qp, &wr, return_value_ptr);
}

/* Polls one CQ and stores either the completion count or a negative operation error. */
extern "C" __global__ __aicore__ void nds_aiv_poll_cq_kernel(GM_ADDR qp_address, uint32_t is_send_cq,
                                                             uint32_t max_completions, GM_ADDR wc_address,
                                                             GM_ADDR return_value) {
    __gm__ const auto *qp = reinterpret_cast<__gm__ const NdsQpDescriptor *>(qp_address);
    __gm__ auto *wc = reinterpret_cast<__gm__ NdsWc *>(wc_address);
    __gm__ auto *return_value_ptr = reinterpret_cast<__gm__ int32_t *>(return_value);
    nds_aiv_poll_cq(qp, is_send_cq, max_completions, wc, return_value_ptr);
}

/* Submits one transport send WR and stores the operation return value. */
extern "C" __global__ __aicore__ void nds_aiv_rdma_send_kernel(GM_ADDR transport_address, GM_ADDR wr_address,
                                                               uint32_t queue_index, GM_ADDR return_value) {
    __gm__ const auto *transport = reinterpret_cast<__gm__ const NdsTransportDescriptor *>(transport_address);
    __gm__ const auto *wr = reinterpret_cast<__gm__ const NdsSendWr *>(wr_address);
    __gm__ auto *return_value_ptr = reinterpret_cast<__gm__ int32_t *>(return_value);
    if (transport == nullptr || wr == nullptr)
        return SetInvalid(return_value_ptr);
    TPipe pipe;
    TBuf<> scratch;
    pipe.InitBuffer(scratch, 64U);
    NdsSendWr send_wr = LoadSendWr(wr);
    nds_aiv_rdma_send(transport, queue_index, &send_wr, return_value_ptr, &scratch);
}

/* Submits one transport receive WR and stores the operation return value. */
extern "C" __global__ __aicore__ void nds_aiv_rdma_recv_kernel(GM_ADDR transport_address, GM_ADDR wr_address,
                                                               uint32_t queue_index, GM_ADDR return_value) {
    __gm__ const auto *transport = reinterpret_cast<__gm__ const NdsTransportDescriptor *>(transport_address);
    __gm__ const auto *wr = reinterpret_cast<__gm__ const NdsRecvWr *>(wr_address);
    __gm__ auto *return_value_ptr = reinterpret_cast<__gm__ int32_t *>(return_value);
    if (transport == nullptr || wr == nullptr)
        return SetInvalid(return_value_ptr);
    NdsRecvWr recv{};
    recv.wr_id = wr->wr_id;
    recv.local.address = wr->local.address;
    recv.local.length = wr->local.length;
    recv.local.local_key = wr->local.local_key;
    nds_aiv_rdma_recv(transport, queue_index, &recv, return_value_ptr);
}

/* Submits one transport RDMA-read WR and stores the operation return value. */
extern "C" __global__ __aicore__ void nds_aiv_rdma_read_kernel(GM_ADDR transport_address, GM_ADDR wr_address,
                                                               uint32_t queue_index, GM_ADDR return_value) {
    __gm__ const auto *transport = reinterpret_cast<__gm__ const NdsTransportDescriptor *>(transport_address);
    __gm__ const auto *wr = reinterpret_cast<__gm__ const NdsSendWr *>(wr_address);
    __gm__ auto *return_value_ptr = reinterpret_cast<__gm__ int32_t *>(return_value);
    if (transport == nullptr || wr == nullptr)
        return SetInvalid(return_value_ptr);
    TPipe pipe;
    TBuf<> scratch;
    pipe.InitBuffer(scratch, 64U);
    NdsSendWr send_wr = LoadSendWr(wr);
    nds_aiv_rdma_read(transport, queue_index, &send_wr, return_value_ptr, &scratch);
}

/* Submits one transport RDMA-write WR and stores the operation return value. */
extern "C" __global__ __aicore__ void nds_aiv_rdma_write_kernel(GM_ADDR transport_address, GM_ADDR wr_address,
                                                                uint32_t queue_index, GM_ADDR return_value) {
    __gm__ const auto *transport = reinterpret_cast<__gm__ const NdsTransportDescriptor *>(transport_address);
    __gm__ const auto *wr = reinterpret_cast<__gm__ const NdsSendWr *>(wr_address);
    __gm__ auto *return_value_ptr = reinterpret_cast<__gm__ int32_t *>(return_value);
    if (transport == nullptr || wr == nullptr)
        return SetInvalid(return_value_ptr);
    TPipe pipe;
    TBuf<> scratch;
    pipe.InitBuffer(scratch, 64U);
    NdsSendWr send_wr = LoadSendWr(wr);
    nds_aiv_rdma_write(transport, queue_index, &send_wr, return_value_ptr, &scratch);
}

/* Submits a transport send batch against one queue in the complete descriptor. */
extern "C" __global__ __aicore__ void nds_aiv_rdma_send_batch_kernel(GM_ADDR transport_address, GM_ADDR wrs_address,
                                                                     uint32_t queue_index, uint32_t wr_count,
                                                                     GM_ADDR bad_wr_address, GM_ADDR return_value) {
    __gm__ const auto *transport = reinterpret_cast<__gm__ const NdsTransportDescriptor *>(transport_address);
    __gm__ const auto *wrs = reinterpret_cast<__gm__ const NdsSendWr *>(wrs_address);
    __gm__ auto *bad_wr = reinterpret_cast<__gm__ uint64_t *>(bad_wr_address);
    __gm__ auto *return_value_ptr = reinterpret_cast<__gm__ int32_t *>(return_value);
    if (transport == nullptr || wrs == nullptr || bad_wr == nullptr)
        return SetInvalid(return_value_ptr);
    TPipe pipe;
    TBuf<> scratch;
    pipe.InitBuffer(scratch, 64U);
    nds_aiv_rdma_send_batch(transport, queue_index, wrs, wr_count, return_value_ptr, bad_wr, &scratch);
}

/* Executes one serialized storage read and stores the operation return value. */
extern "C" __global__ __aicore__ void nds_aiv_storage_read_kernel(GM_ADDR context_address, GM_ADDR command_address,
                                                                  GM_ADDR return_value) {
    __gm__ const auto *context = reinterpret_cast<__gm__ const NdsStorageDescriptor *>(context_address);
    __gm__ const auto *command = reinterpret_cast<__gm__ const nds::StorageReadCommand *>(command_address);
    __gm__ auto *return_value_ptr = reinterpret_cast<__gm__ int32_t *>(return_value);
    TPipe pipe;
    TBuf<> scratch;
    pipe.InitBuffer(scratch, 64U);
    nds_aiv_storage_read(context, command, return_value_ptr, &scratch);
}

/* Executes one serialized storage write and stores the operation return value. */
extern "C" __global__ __aicore__ void nds_aiv_storage_write_kernel(GM_ADDR context_address, GM_ADDR command_address,
                                                                   GM_ADDR return_value) {
    __gm__ const auto *context = reinterpret_cast<__gm__ const NdsStorageDescriptor *>(context_address);
    __gm__ const auto *command = reinterpret_cast<__gm__ const nds::StorageWriteCommand *>(command_address);
    __gm__ auto *return_value_ptr = reinterpret_cast<__gm__ int32_t *>(return_value);
    TPipe pipe;
    TBuf<> scratch;
    pipe.InitBuffer(scratch, 64U);
    nds_aiv_storage_write(context, command, return_value_ptr, &scratch);
}

/* Executes one serialized storage batch-read and stores the operation return value. */
extern "C" __global__ __aicore__ void nds_aiv_storage_batch_read_kernel(GM_ADDR context_address,
                                                                        GM_ADDR command_address, GM_ADDR return_value) {
    __gm__ const auto *context = reinterpret_cast<__gm__ const NdsStorageDescriptor *>(context_address);
    __gm__ const auto *command = reinterpret_cast<__gm__ const nds::StorageBatchReadCommand *>(command_address);
    __gm__ auto *return_value_ptr = reinterpret_cast<__gm__ int32_t *>(return_value);
    TPipe pipe;
    TBuf<> scratch;
    pipe.InitBuffer(scratch, 64U);
    nds_aiv_storage_batch_read(context, command, return_value_ptr, &scratch);
}

/* Executes one serialized storage batch-write and stores the operation return value. */
extern "C" __global__ __aicore__ void nds_aiv_storage_batch_write_kernel(GM_ADDR context_address,
                                                                         GM_ADDR command_address,
                                                                         GM_ADDR return_value) {
    __gm__ const auto *context = reinterpret_cast<__gm__ const NdsStorageDescriptor *>(context_address);
    __gm__ const auto *command = reinterpret_cast<__gm__ const nds::StorageBatchWriteCommand *>(command_address);
    __gm__ auto *return_value_ptr = reinterpret_cast<__gm__ int32_t *>(return_value);
    TPipe pipe;
    TBuf<> scratch;
    pipe.InitBuffer(scratch, 64U);
    nds_aiv_storage_batch_write(context, command, return_value_ptr, &scratch);
}

/* Waits for a storage completion and stores the operation return value. */
extern "C" __global__ __aicore__ void nds_aiv_storage_wait_kernel(GM_ADDR context_address, uint64_t command_id,
                                                                  uint64_t expected_bytes, uint32_t slot_index,
                                                                  GM_ADDR return_value) {
    __gm__ const auto *context = reinterpret_cast<__gm__ const NdsStorageDescriptor *>(context_address);
    __gm__ auto *return_value_ptr = reinterpret_cast<__gm__ int32_t *>(return_value);
    nds_aiv_storage_wait(context, command_id, expected_bytes, slot_index, return_value_ptr);
}

static const struct FunLevelKType nds_aiv_post_send_kernel_type_section __attribute__((
    used, section(".ascend.meta.nds_aiv_post_send_kernel"))) = {{F_TYPE_KTYPE, sizeof(unsigned int), K_TYPE_AIV}};
static const struct FunLevelKType nds_aiv_post_send_batch_kernel_type_section __attribute__((
    used, section(".ascend.meta.nds_aiv_post_send_batch_kernel"))) = {{F_TYPE_KTYPE, sizeof(unsigned int), K_TYPE_AIV}};
static const struct FunLevelKType nds_aiv_post_recv_kernel_type_section __attribute__((
    used, section(".ascend.meta.nds_aiv_post_recv_kernel"))) = {{F_TYPE_KTYPE, sizeof(unsigned int), K_TYPE_AIV}};
static const struct FunLevelKType nds_aiv_poll_cq_kernel_type_section __attribute__((
    used, section(".ascend.meta.nds_aiv_poll_cq_kernel"))) = {{F_TYPE_KTYPE, sizeof(unsigned int), K_TYPE_AIV}};
static const struct FunLevelKType nds_aiv_rdma_send_kernel_type_section __attribute__((
    used, section(".ascend.meta.nds_aiv_rdma_send_kernel"))) = {{F_TYPE_KTYPE, sizeof(unsigned int), K_TYPE_AIV}};
static const struct FunLevelKType nds_aiv_rdma_recv_kernel_type_section __attribute__((
    used, section(".ascend.meta.nds_aiv_rdma_recv_kernel"))) = {{F_TYPE_KTYPE, sizeof(unsigned int), K_TYPE_AIV}};
static const struct FunLevelKType nds_aiv_rdma_read_kernel_type_section __attribute__((
    used, section(".ascend.meta.nds_aiv_rdma_read_kernel"))) = {{F_TYPE_KTYPE, sizeof(unsigned int), K_TYPE_AIV}};
static const struct FunLevelKType nds_aiv_rdma_write_kernel_type_section __attribute__((
    used, section(".ascend.meta.nds_aiv_rdma_write_kernel"))) = {{F_TYPE_KTYPE, sizeof(unsigned int), K_TYPE_AIV}};
static const struct FunLevelKType nds_aiv_rdma_send_batch_kernel_type_section __attribute__((
    used, section(".ascend.meta.nds_aiv_rdma_send_batch_kernel"))) = {{F_TYPE_KTYPE, sizeof(unsigned int), K_TYPE_AIV}};
static const struct FunLevelKType nds_aiv_storage_read_kernel_type_section __attribute__((
    used, section(".ascend.meta.nds_aiv_storage_read_kernel"))) = {{F_TYPE_KTYPE, sizeof(unsigned int), K_TYPE_AIV}};
static const struct FunLevelKType nds_aiv_storage_write_kernel_type_section __attribute__((
    used, section(".ascend.meta.nds_aiv_storage_write_kernel"))) = {{F_TYPE_KTYPE, sizeof(unsigned int), K_TYPE_AIV}};
static const struct FunLevelKType nds_aiv_storage_batch_read_kernel_type_section
    __attribute__((used, section(".ascend.meta.nds_aiv_storage_batch_read_kernel"))) = {
        {F_TYPE_KTYPE, sizeof(unsigned int), K_TYPE_AIV}};
static const struct FunLevelKType nds_aiv_storage_batch_write_kernel_type_section
    __attribute__((used, section(".ascend.meta.nds_aiv_storage_batch_write_kernel"))) = {
        {F_TYPE_KTYPE, sizeof(unsigned int), K_TYPE_AIV}};
static const struct FunLevelKType nds_aiv_storage_wait_kernel_type_section __attribute__((
    used, section(".ascend.meta.nds_aiv_storage_wait_kernel"))) = {{F_TYPE_KTYPE, sizeof(unsigned int), K_TYPE_AIV}};
