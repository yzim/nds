#ifndef NDS_CLIENT_LAUNCH_HH
#define NDS_CLIENT_LAUNCH_HH

#include "connection.hh"
#include "nds/npu_ra_qp.hh"
#include "nds/result.hh"

#include <cstdint>

namespace nds {

class NpuRaContext;

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

bool dataplane_supports(NpuExecutionMode execution, WorkRequestOpcode opcode) noexcept;
bool dataplane_supports_post_recv(NpuExecutionMode execution) noexcept;
bool dataplane_supports_cq_poll(NpuExecutionMode execution, CompletionQueue queue) noexcept;
Result<void> post_send_wr(NpuRaContext *context, NpuRaQp *qp, NpuExecutionMode execution,
                          const client::RmaConfig &config, const WorkRequest &request);
Result<void> post_recv_wr(NpuRaContext *context, NpuRaQp *qp, NpuExecutionMode execution,
                          const client::RmaConfig &config, const ReceiveRequest &request);
Result<std::uint32_t> poll_cq(NpuRaContext *context, NpuRaQp *qp, NpuExecutionMode execution,
                              const client::RmaConfig &config, CompletionQueue queue,
                              nds_ra_completion *completions, std::uint32_t max_entries);

}  // namespace nds

namespace nds::client {

/* Host-example connection data plane. Invokes Host RA or launches AIV/AICPU. */
Result<void> send(Connection *session, const RegisteredRegion &source, std::uint32_t length);
Result<void> post_receive(Connection *session, const RegisteredRegion &destination, std::uint64_t wr_id);
Result<std::uint32_t> poll(Connection *session, CompletionQueue queue, nds_ra_completion *completions,
                           std::uint32_t max_entries);
Result<void> read(Connection *session, const RegisteredRegion &local, std::uint64_t remote_address,
                  std::uint32_t remote_key, std::uint32_t length);
Result<void> write(Connection *session, const RegisteredRegion &local, std::uint64_t remote_address,
                   std::uint32_t remote_key, std::uint32_t length);

}  // namespace nds::client

#endif
