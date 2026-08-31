#include "api.h"
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
    auto post = reinterpret_cast<NdsHnsPostRecvFn>(NdsAicpuResolveSymbol("ibv_post_recv"));
    if (post != nullptr) {
        NdsHnsSge sge{wr->local.address, wr->local.length, wr->local.local_key};
        NdsHnsRecvWr provider_wr{wr->wr_id, nullptr, &sge, 1, 0U};
        NdsHnsRecvWr *bad = nullptr;
        const int provider_result = post(reinterpret_cast<void *>(qp->provider_qp_address), &provider_wr, &bad);
        NdsAicpuSetReturnValue(
            return_value, provider_result == 0 ? NDS_DEVICE_OPERATION_SUCCESS : NDS_DEVICE_OPERATION_PROVIDER_FAILED);
        return kNdsAicpuSuccess;
    }
    // The provider owns its private WR-ID table. The exported AI-QP metadata
    // does not disclose that table, so a raw RQ fallback cannot be correct.
    NdsAicpuSetReturnValue(return_value, NDS_DEVICE_OPERATION_SYMBOL_UNAVAILABLE);
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
    auto poll = reinterpret_cast<NdsHnsPollCqFn>(NdsAicpuResolveSymbol("ibv_poll_cq"));
    const uint32_t limit = max_completions < NDS_DEVICE_MAX_COMPLETIONS ? max_completions : NDS_DEVICE_MAX_COMPLETIONS;
    if (poll != nullptr) {
        NdsHnsWc completions[NDS_DEVICE_MAX_COMPLETIONS]{};
        void *cq =
            reinterpret_cast<void *>(is_send_cq ? qp->provider_send_cq_address : qp->provider_receive_cq_address);
        const int count = poll(cq, static_cast<int>(limit), completions);
        if (count < 0) {
            NdsAicpuSetReturnValue(return_value, NDS_DEVICE_OPERATION_PROVIDER_FAILED);
            return kNdsAicpuSuccess;
        }
        for (int index = 0; index < count; ++index) {
            wc[index] = {completions[index].wr_id,      completions[index].status,   completions[index].opcode,
                         completions[index].vendor_err, completions[index].byte_len, completions[index].qp_num,
                         completions[index].wc_flags,   completions[index].imm_data, 0U};
        }
        *return_value = count;
        return kNdsAicpuSuccess;
    }
    // See post_recv: without the provider's private WR-ID table this raw CQ
    // path cannot produce a valid completion identity.
    NdsAicpuSetReturnValue(return_value, NDS_DEVICE_OPERATION_SYMBOL_UNAVAILABLE);
    return kNdsAicpuSuccess;
}
