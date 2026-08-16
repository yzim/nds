#include "launch.hh"

#include <cassert>

int main() {
    using nds::NpuExecutionMode;
    using nds::CompletionQueue;
    using nds::WorkRequestOpcode;

    assert(nds::dataplane_supports(NpuExecutionMode::HostRa, WorkRequestOpcode::Send));
    assert(nds::dataplane_supports(NpuExecutionMode::HostRa, WorkRequestOpcode::RdmaRead));
    assert(nds::dataplane_supports(NpuExecutionMode::HostRa, WorkRequestOpcode::RdmaWrite));
    assert(nds::dataplane_supports_cq_poll(NpuExecutionMode::HostRa, nds::CompletionQueue::Send));
    assert(nds::dataplane_supports_cq_poll(NpuExecutionMode::HostRa, CompletionQueue::Send));
    assert(!nds::dataplane_supports_cq_poll(NpuExecutionMode::HostRa, CompletionQueue::Receive));

    assert(nds::dataplane_supports(NpuExecutionMode::Aicpu, WorkRequestOpcode::Send));
    assert(nds::dataplane_supports(NpuExecutionMode::Aicpu, WorkRequestOpcode::RdmaRead));
    assert(nds::dataplane_supports(NpuExecutionMode::Aicpu, WorkRequestOpcode::RdmaWrite));
    assert(nds::dataplane_supports_cq_poll(NpuExecutionMode::Aicpu, nds::CompletionQueue::Send));
    assert(nds::dataplane_supports_post_recv(NpuExecutionMode::Aicpu));
    assert(nds::dataplane_supports_cq_poll(NpuExecutionMode::Aicpu, CompletionQueue::Send));
    assert(nds::dataplane_supports_cq_poll(NpuExecutionMode::Aicpu, CompletionQueue::Receive));

    assert(nds::dataplane_supports(NpuExecutionMode::Aiv, WorkRequestOpcode::Send));
    assert(nds::dataplane_supports(NpuExecutionMode::Aiv, WorkRequestOpcode::RdmaRead));
    assert(nds::dataplane_supports(NpuExecutionMode::Aiv, WorkRequestOpcode::RdmaWrite));
    assert(nds::dataplane_supports_cq_poll(NpuExecutionMode::Aiv, nds::CompletionQueue::Send));
    assert(nds::dataplane_supports_post_recv(NpuExecutionMode::Aiv));
    assert(nds::dataplane_supports_cq_poll(NpuExecutionMode::Aiv, CompletionQueue::Send));
    assert(nds::dataplane_supports_cq_poll(NpuExecutionMode::Aiv, CompletionQueue::Receive));
    assert(!nds::dataplane_supports(NpuExecutionMode::HostRa, static_cast<WorkRequestOpcode>(99)));
    return 0;
}
