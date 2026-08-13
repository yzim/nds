#include "storage_execution.hh"

#include "nds/logging.hh"

#include <cerrno>
#include <cstring>
#include <ctime>

namespace nds {

bool poll_cpu_completion(ibv_cq *cq, ibv_wc_opcode expected_opcode,
                         std::uint32_t timeout_ms, const char *component)
{
    const timespec delay{0, 1000000L};
    for (std::uint32_t elapsed = 0U; elapsed < timeout_ms; ++elapsed) {
        ibv_wc completion{};
        const int count = ibv_poll_cq(cq, 1, &completion);
        if (count < 0 || (count == 1 &&
            (completion.status != IBV_WC_SUCCESS || completion.opcode != expected_opcode))) {
            NDS_LOG_ERROR(component, "unexpected CQ completion: count={} status={} opcode={}", count,
                          count == 1 ? completion.status : -1, count == 1 ? completion.opcode : -1);
            return false;
        }
        if (count == 1) return true;
        (void)nanosleep(&delay, nullptr);
    }
    NDS_LOG_ERROR(component, "timed out waiting for verbs CQ completion");
    return false;
}

bool execute_storage_command(CpuStorageTransport &transport,
                             const nds_storage_command &command,
                             const nds_storage_bootstrap &bootstrap,
                             std::uint32_t timeout_ms, const char *component)
{
    if (transport.qp == nullptr || transport.cq == nullptr || transport.namespace_buffer == nullptr ||
        transport.namespace_mr == nullptr || transport.completion == nullptr || transport.completion_mr == nullptr) {
        NDS_LOG_ERROR(component, "CPU storage transport is incomplete");
        return false;
    }
    ibv_sge data_sge{};
    ibv_sge completion_sge{};
    ibv_send_wr data{};
    ibv_send_wr completion{};
    ibv_send_wr *bad = nullptr;
    nds_storage_completion response{command.request_id, NDS_STORAGE_COMPLETION_COMPLETE,
                                    NDS_STORAGE_SUCCESS, command.length};
    char error[NDS_STORAGE_ERROR_CAPACITY]{};

    if (command.offset > transport.namespace_buffer->size() ||
        command.length > transport.namespace_buffer->size() - command.offset) {
        response.status = NDS_STORAGE_RANGE_ERROR;
        response.bytes_transferred = 0U;
    }
    if (nds_storage_completion_encode(&response, transport.completion, error) != 0) {
        NDS_LOG_ERROR(component, "cannot encode completion: {}", error);
        return false;
    }
    completion_sge.addr = reinterpret_cast<std::uintptr_t>(transport.completion);
    completion_sge.length = sizeof(*transport.completion);
    completion_sge.lkey = transport.completion_mr->lkey;
    completion.wr_id = 3U;
    completion.sg_list = &completion_sge;
    completion.num_sge = 1;
    completion.opcode = IBV_WR_RDMA_WRITE;
    completion.send_flags = IBV_SEND_SIGNALED;
    completion.wr.rdma.remote_addr = bootstrap.completion.address;
    completion.wr.rdma.rkey = bootstrap.completion.rkey;
    if (response.status == NDS_STORAGE_SUCCESS) {
        data_sge.addr = reinterpret_cast<std::uintptr_t>(transport.namespace_buffer->data() + command.offset);
        data_sge.length = static_cast<std::uint32_t>(command.length);
        data_sge.lkey = transport.namespace_mr->lkey;
        data.wr_id = 2U;
        data.sg_list = &data_sge;
        data.num_sge = 1;
        data.opcode = command.operation == NDS_STORAGE_READ ? IBV_WR_RDMA_WRITE : IBV_WR_RDMA_READ;
        data.wr.rdma.remote_addr = command.data.address;
        data.wr.rdma.rkey = command.data.rkey;
        data.next = &completion;
        if (ibv_post_send(transport.qp, &data, &bad) != 0) {
            NDS_LOG_ERROR(component, "ibv_post_send(storage data): {}", std::strerror(errno));
            return false;
        }
    } else if (ibv_post_send(transport.qp, &completion, &bad) != 0) {
        NDS_LOG_ERROR(component, "ibv_post_send(range completion): {}", std::strerror(errno));
        return false;
    }
    return poll_cpu_completion(transport.cq, IBV_WC_RDMA_WRITE, timeout_ms, component);
}

} // namespace nds
