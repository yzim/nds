#ifndef NDS_CLIENT_RMA_HH
#define NDS_CLIENT_RMA_HH

#include "nds/npu_ra_qp.hh"
#include "nds/result.hh"

#include <cstdint>
#include <string>

namespace nds {

class NpuRaContext;

struct RmaConfig {
    std::string aicpu_kernel_config;
    std::string aiv_kernel;
};

enum class WorkRequestOpcode {
    Send,
    RdmaRead,
    RdmaWrite,
};

struct WorkRequest {
    WorkRequestOpcode opcode{WorkRequestOpcode::Send};
    nds_ra_sge local{};
    std::uint64_t remote_address{};
    std::uint32_t remote_key{};
};

/* A receive request only describes the local registered buffer. */
struct ReceiveRequest {
    nds_ra_sge local{};
    std::uint64_t wr_id{};
};

enum class CompletionQueue {
    Send,
    Receive,
};

bool rma_supports(NpuExecutionMode execution, WorkRequestOpcode opcode) noexcept;
bool rma_supports_post_recv(NpuExecutionMode execution) noexcept;
bool rma_supports_cq_poll(NpuExecutionMode execution, CompletionQueue queue) noexcept;
Result<void> post_send_wr(NpuRaContext *context, NpuRaQp *qp, NpuExecutionMode execution,
                          const RmaConfig &config, const WorkRequest &request);
Result<void> post_recv_wr(NpuRaContext *context, NpuRaQp *qp, NpuExecutionMode execution,
                          const RmaConfig &config, const ReceiveRequest &request);
Result<std::uint32_t> poll_cq(NpuRaContext *context, NpuRaQp *qp, NpuExecutionMode execution,
                              const RmaConfig &config, CompletionQueue queue,
                              nds_ra_completion *completions, std::uint32_t max_entries);

}  // namespace nds

#endif
