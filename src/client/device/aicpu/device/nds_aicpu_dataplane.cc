#include "nds_aicpu_device_api.h"
#include "nds_aicpu_hns_abi.h"
#include "nds/device_hns_codec.h"

#include <dlfcn.h>
#include <stdint.h>

namespace {
constexpr uint32_t kVerbsSuccess = 0U;
constexpr uint32_t kVerbsInvalidArgument = 1U;
constexpr char kHnsProviderLibrary[] = "libhns-rdmav25.so";
constexpr char kVerbsLibrary[] = "libibverbs.so.1";

void *resolve_symbol(const char *name) {
    static void *provider = dlopen(kHnsProviderLibrary, RTLD_NOW | RTLD_LOCAL);
    if (provider != nullptr) {
        void *symbol = dlsym(provider, name);
        if (symbol != nullptr) return symbol;
    }
    static void *verbs = dlopen(kVerbsLibrary, RTLD_NOW | RTLD_LOCAL);
    return verbs == nullptr ? nullptr : dlsym(verbs, name);
}

void verbs_barrier() {
#if defined(__aarch64__)
    asm volatile("dsb st" ::: "memory");
#else
    asm volatile("" ::: "memory");
#endif
}

bool valid_qp(const nds_device_qp *qp) {
    return qp != nullptr && qp->abi_version == NDS_DEVICE_QP_ABI_VERSION && qp->size == sizeof(*qp);
}

int32_t provider_opcode(uint32_t opcode) {
    if (opcode == NDS_DEVICE_WR_SEND) return NDS_HNS_WR_SEND;
    if (opcode == NDS_DEVICE_WR_RDMA_READ) return NDS_HNS_WR_RDMA_READ;
    if (opcode == NDS_DEVICE_WR_RDMA_WRITE) return NDS_HNS_WR_RDMA_WRITE;
    return -1;
}

void set_result(nds_device_operation_result *result, uint32_t status, uint32_t path,
                int32_t provider_result) {
    result->status = status;
    result->path = path;
    result->provider_result = provider_result;
    result->reserved = 0U;
    verbs_barrier();
}
}  // namespace

extern "C" __attribute__((visibility("default"))) uint32_t NdsAicpuPostSend(
    const nds_device_qp *qp, const nds_device_send_wr *wr, nds_device_operation_result *result) {
    if (!valid_qp(qp) || wr == nullptr || result == nullptr) return kVerbsInvalidArgument;
    const int32_t opcode = provider_opcode(wr->opcode);
    if (opcode < 0) {
        set_result(result, NDS_DEVICE_OPERATION_UNSUPPORTED, NDS_DEVICE_OPERATION_PATH_NONE, 0);
        return kVerbsSuccess;
    }
    auto post = reinterpret_cast<nds_hns_exp_post_send_fn>(resolve_symbol("ibv_exp_post_send"));
    if (post == nullptr) {
        set_result(result, NDS_DEVICE_OPERATION_SYMBOL_UNAVAILABLE,
                   NDS_DEVICE_OPERATION_PATH_PROVIDER, 0);
        return kVerbsSuccess;
    }
    nds_hns_sge sge{wr->local.address, wr->local.length, wr->local.local_key};
    nds_hns_send_wr provider_wr{};
    provider_wr.wr_id = wr->wr_id;
    provider_wr.sg_list = &sge;
    provider_wr.num_sge = 1;
    provider_wr.opcode = opcode;
    provider_wr.send_flags = (wr->flags & NDS_DEVICE_SEND_SIGNALED) != 0U ?
                                 static_cast<uint32_t>(NDS_HNS_SEND_SIGNALED) : 0U;
    provider_wr.rdma.remote_addr = wr->remote_address;
    provider_wr.rdma.rkey = wr->remote_key;
    nds_hns_send_wr *bad = nullptr;
    nds_hns_post_send_response response{};
    const int provider_result = post(reinterpret_cast<void *>(qp->provider_qp_address),
                                     &provider_wr, &bad, &response);
    if (provider_result == 0 && response.db_info != 0UL)
        *reinterpret_cast<volatile uint64_t *>(qp->send_queue.doorbell_address) =
            static_cast<uint64_t>(response.db_info);
    set_result(result,
               provider_result == 0 ? NDS_DEVICE_OPERATION_SUCCESS : NDS_DEVICE_OPERATION_PROVIDER_FAILED,
               NDS_DEVICE_OPERATION_PATH_PROVIDER, provider_result);
    return kVerbsSuccess;
}

extern "C" __attribute__((visibility("default"))) uint32_t NdsAicpuPostRecv(
    const nds_device_qp *qp, const nds_device_recv_wr *wr, nds_device_operation_result *result) {
    if (!valid_qp(qp) || wr == nullptr || result == nullptr) return kVerbsInvalidArgument;
    auto post = reinterpret_cast<nds_hns_post_recv_fn>(resolve_symbol("ibv_post_recv"));
    if (post != nullptr) {
        nds_hns_sge sge{wr->local.address, wr->local.length, wr->local.local_key};
        nds_hns_recv_wr provider_wr{wr->wr_id, nullptr, &sge, 1, 0U};
        nds_hns_recv_wr *bad = nullptr;
        const int provider_result = post(reinterpret_cast<void *>(qp->provider_qp_address),
                                         &provider_wr, &bad);
        set_result(result,
                   provider_result == 0 ? NDS_DEVICE_OPERATION_SUCCESS : NDS_DEVICE_OPERATION_PROVIDER_FAILED,
                   NDS_DEVICE_OPERATION_PATH_PROVIDER, provider_result);
        return kVerbsSuccess;
    }
    const nds_device_work_queue &queue = qp->receive_queue;
    auto *head = reinterpret_cast<uint32_t *>(queue.head_address);
    auto *tail = reinterpret_cast<uint32_t *>(queue.tail_address);
    if (!nds_hns_queue_has_space(*head, *tail, queue.depth, 0U)) {
        set_result(result, NDS_DEVICE_OPERATION_QUEUE_FULL, NDS_DEVICE_OPERATION_PATH_DIRECT, 0);
        return kVerbsSuccess;
    }
    const uint32_t index = *head % queue.depth;
    auto *segment = reinterpret_cast<nds_hns_receive_segment *>(
        queue.buffer_address + static_cast<uint64_t>(queue.entry_size) * index);
    nds_hns_encode_receive_segment(segment, wr->local.address, wr->local.length, wr->local.local_key);
    reinterpret_cast<uint64_t *>(queue.wr_id_address)[index] = wr->wr_id;
    verbs_barrier();
    ++*head;
    *reinterpret_cast<uint32_t *>(queue.doorbell_address) = *head & 0xffffU;
    set_result(result, NDS_DEVICE_OPERATION_SUCCESS, NDS_DEVICE_OPERATION_PATH_DIRECT, 0);
    return kVerbsSuccess;
}

extern "C" __attribute__((visibility("default"))) uint32_t NdsAicpuPollCq(
    const nds_device_qp *qp, const nds_device_poll_cq_request *request,
    nds_device_operation_result *result) {
    if (!valid_qp(qp) || request == nullptr || result == nullptr ||
        request->completion_output_address == 0U || request->max_completions == 0U)
        return kVerbsInvalidArgument;
    auto poll = reinterpret_cast<nds_hns_poll_cq_fn>(resolve_symbol("ibv_poll_cq"));
    const uint32_t limit = request->max_completions < NDS_DEVICE_MAX_COMPLETIONS ?
                               request->max_completions : NDS_DEVICE_MAX_COMPLETIONS;
    auto *output = reinterpret_cast<nds_device_completion_output *>(request->completion_output_address);
    if (poll != nullptr) {
        nds_hns_wc completions[NDS_DEVICE_MAX_COMPLETIONS]{};
        void *cq = reinterpret_cast<void *>(request->queue_kind == NDS_DEVICE_SEND_QUEUE ?
                                               qp->provider_send_cq_address :
                                               qp->provider_receive_cq_address);
        const int count = poll(cq, static_cast<int>(limit), completions);
        if (count < 0) {
            set_result(result, NDS_DEVICE_OPERATION_PROVIDER_FAILED,
                       NDS_DEVICE_OPERATION_PATH_PROVIDER, count);
            return kVerbsSuccess;
        }
        for (int index = 0; index < count; ++index) {
            output->entries[index] = {completions[index].wr_id, completions[index].status,
                                      completions[index].opcode, completions[index].vendor_err,
                                      completions[index].byte_len, completions[index].qp_num,
                                      completions[index].wc_flags, completions[index].imm_data, 0U};
        }
        output->count = static_cast<uint32_t>(count);
        set_result(result, NDS_DEVICE_OPERATION_SUCCESS, NDS_DEVICE_OPERATION_PATH_PROVIDER, 0);
        return kVerbsSuccess;
    }
    const bool receive = request->queue_kind == NDS_DEVICE_RECEIVE_QUEUE;
    const nds_device_completion_queue &cq = receive ? qp->receive_cq : qp->send_cq;
    const nds_device_work_queue &wq = receive ? qp->receive_queue : qp->send_queue;
    auto *consumer_address = reinterpret_cast<uint32_t *>(cq.consumer_address);
    auto *tail_address = reinterpret_cast<uint32_t *>(wq.tail_address);
    uint32_t consumer = *consumer_address;
    uint32_t tail = *tail_address;
    uint32_t count = 0U;
    while (count < limit) {
        auto *cqe = reinterpret_cast<nds_hns_cqe *>(
            cq.buffer_address + static_cast<uint64_t>(cq.entry_size) * (consumer % cq.depth));
        if (!nds_hns_cqe_is_ready(cqe, consumer, cq.depth)) break;
        if (!receive) tail = nds_hns_send_tail_for_cqe(tail, wq.depth, cqe);
        nds_hns_decode_cqe(cqe, reinterpret_cast<uint64_t *>(wq.wr_id_address)[tail % wq.depth],
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
    set_result(result, NDS_DEVICE_OPERATION_SUCCESS, NDS_DEVICE_OPERATION_PATH_DIRECT, 0);
    return kVerbsSuccess;
}

namespace {
constexpr uint32_t kConnectionInvalidArgument = 1U;

bool valid_connection(const nds_device_connection *connection,
                      const nds_device_transfer *transfer,
                      nds_device_operation_result *result) {
    return connection != nullptr && transfer != nullptr && result != nullptr &&
           connection->abi_version == NDS_DEVICE_CONNECTION_ABI_VERSION &&
           connection->size == sizeof(*connection);
}

uint32_t post(const nds_device_connection *connection, const nds_device_transfer *transfer,
              uint32_t opcode, nds_device_operation_result *result) {
    if (!valid_connection(connection, transfer, result)) return kConnectionInvalidArgument;
    nds_device_send_wr wr{};
    nds_device_build_send_wr(transfer, opcode, &wr);
    return NdsAicpuPostSend(&connection->qp, &wr, result);
}
}  // namespace

extern "C" __attribute__((visibility("default"))) uint32_t NdsAicpuRdmaSend(
    const nds_device_connection *connection, const nds_device_transfer *transfer,
    nds_device_operation_result *result) {
    return post(connection, transfer, NDS_DEVICE_WR_SEND, result);
}

extern "C" __attribute__((visibility("default"))) uint32_t NdsAicpuRdmaRecv(
    const nds_device_connection *connection, const nds_device_transfer *transfer,
    nds_device_operation_result *result) {
    if (!valid_connection(connection, transfer, result)) return kConnectionInvalidArgument;
    nds_device_recv_wr wr{};
    nds_device_build_recv_wr(transfer, &wr);
    return NdsAicpuPostRecv(&connection->qp, &wr, result);
}

extern "C" __attribute__((visibility("default"))) uint32_t NdsAicpuRdmaRead(
    const nds_device_connection *connection, const nds_device_transfer *transfer,
    nds_device_operation_result *result) {
    return post(connection, transfer, NDS_DEVICE_WR_RDMA_READ, result);
}

extern "C" __attribute__((visibility("default"))) uint32_t NdsAicpuRdmaWrite(
    const nds_device_connection *connection, const nds_device_transfer *transfer,
    nds_device_operation_result *result) {
    return post(connection, transfer, NDS_DEVICE_WR_RDMA_WRITE, result);
}
