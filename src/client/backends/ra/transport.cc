#include "ra.hh"

#include <chrono>
#include <thread>

namespace nds {

namespace {
constexpr std::uint32_t kTransportPollBatch = NDS_MAX_COMPLETIONS;
constexpr std::uint32_t kTransportCompletionTimeoutMs = 5000U;

Result<std::uint32_t> reclaim(client::QueuePair *qp, NdsTransportQpState *state, bool send_cq) {
    if (qp == nullptr || state == nullptr)
        return Error{ErrorCode::kInvalidArgument, "RA transport completion state is missing"};
    NdsWc completions[kTransportPollBatch]{};
    NDS_ASSIGN_OR_RETURN(const std::uint32_t count, NdsRaPollCq(qp, send_cq, kTransportPollBatch, completions));
    bool completion_failed = false;
    for (std::uint32_t index = 0U; index < count; ++index)
        completion_failed = completion_failed || completions[index].status != NDS_WC_SUCCESS;
    if (send_cq)
        nds_transport_reclaim_send(state, count);
    else
        nds_transport_reclaim_receive(state, count);
    if (completion_failed)
        return Error{ErrorCode::kRa, "RA transport completion failed"};
    return count;
}

Result<void> wait_for_send_completion(client::QueuePair *qp, NdsTransportQpState *state, std::uint32_t target_credits) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(kTransportCompletionTimeoutMs);
    while (state->send_credits < target_credits && std::chrono::steady_clock::now() < deadline) {
        NDS_RETURN_IF_ERROR(reclaim(qp, state, true));
        if (state->send_credits < target_credits)
            std::this_thread::yield();
    }
    return state->send_credits >= target_credits
               ? Result<void>{}
               : Result<void>(Error{ErrorCode::kRuntime, "RA transport completion timed out"});
}

Result<void> reclaim_if_needed(client::QueuePair *qp, NdsTransportQpState *state, bool send_cq) {
    if (send_cq && state->send_credits == 0U)
        NDS_RETURN_IF_ERROR(reclaim(qp, state, true));
    if (!send_cq && state->receive_credits == 0U)
        NDS_RETURN_IF_ERROR(reclaim(qp, state, false));
    return {};
}

Result<void> post(client::Runtime *runtime, client::QueuePair *qp, NdsTransportQpState *state,
                  const NdsSendWr &request) {
    if (runtime == nullptr || qp == nullptr || state == nullptr || state->signal_interval == 0U)
        return Error{ErrorCode::kInvalidArgument, "RA transport send requires runtime, QP, and state"};
    NDS_RETURN_IF_ERROR(reclaim_if_needed(qp, state, true));
    if (state->send_credits == 0U)
        return Error{ErrorCode::kTransport, "RA transport send credits exhausted"};

    NdsTransportQpState next = *state;
    const std::uint32_t signaled = nds_transport_should_signal(&next);
    NdsSendWr scheduled = request;
    scheduled.flags = signaled != 0U ? static_cast<std::uint32_t>(NDS_SEND_SIGNALED) : 0U;
    NDS_RETURN_IF_ERROR(NdsRaPostSend(runtime, qp, scheduled, nullptr));
    if (nds_transport_record_send(&next, signaled) == 0U)
        return Error{ErrorCode::kTransport, "RA transport send state update failed"};
    *state = next;
    if (signaled != 0U)
        return wait_for_send_completion(qp, state, state->send_credits + state->signal_interval);
    return {};
}
}  // namespace

Result<void> NdsRaRdmaSend(const RaConnection &connection, NdsTransportQpState *state, const NdsSendWr &request) {
    return post(connection.runtime, connection.qp, state, request);
}

Result<void> NdsRaRdmaRecv(const RaConnection &connection, NdsTransportQpState *state, const NdsRecvWr &request) {
    if (connection.runtime == nullptr || connection.qp == nullptr || state == nullptr)
        return Error{ErrorCode::kInvalidArgument, "RA receive requires runtime, QP, and state"};
    NDS_RETURN_IF_ERROR(reclaim_if_needed(connection.qp, state, false));
    if (state->receive_credits == 0U)
        return Error{ErrorCode::kTransport, "RA transport receive credits exhausted"};
    NDS_RETURN_IF_ERROR(NdsRaPostRecv(connection.qp, request));
    if (nds_transport_record_receive(state) == 0U)
        return Error{ErrorCode::kTransport, "RA transport receive state update failed"};
    return {};
}

Result<void> NdsRaRdmaRead(const RaConnection &connection, NdsTransportQpState *state, const NdsSendWr &request) {
    return post(connection.runtime, connection.qp, state, request);
}

Result<void> NdsRaRdmaWrite(const RaConnection &connection, NdsTransportQpState *state, const NdsSendWr &request) {
    return post(connection.runtime, connection.qp, state, request);
}

}  // namespace nds
