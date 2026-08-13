#ifndef NDS_CPU_STORAGE_EXECUTION_HPP
#define NDS_CPU_STORAGE_EXECUTION_HPP

#include "nds/storage_protocol.h"

#include <infiniband/verbs.h>

#include <cstdint>
#include <vector>

namespace nds {

struct CpuStorageTransport {
    ibv_qp *qp{};
    ibv_cq *cq{};
    std::vector<unsigned char> *namespace_buffer{};
    ibv_mr *namespace_mr{};
    nds_storage_completion_wire *completion{};
    ibv_mr *completion_mr{};
};

bool poll_cpu_completion(ibv_cq *cq, ibv_wc_opcode expected_opcode, std::uint32_t timeout_ms, const char *component);

/* Execute one decoded storage command and publish its terminal completion. */
bool execute_storage_command(CpuStorageTransport *transport, const nds_storage_command &command,
                             const nds_storage_bootstrap &bootstrap, std::uint32_t timeout_ms, const char *component);

}  // namespace nds

#endif
