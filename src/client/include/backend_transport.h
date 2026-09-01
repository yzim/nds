#ifndef NDS_BACKEND_TRANSPORT_H
#define NDS_BACKEND_TRANSPORT_H

#include "backend_verbs.h"

#include <stdint.h>

/* Transport-owned scheduling state associated with one QP. It is separate
 * from the hardware-facing NdsQpDescriptor descriptor. */
typedef struct NdsTransportQpState {
    uint32_t signal_interval;
    uint32_t unsignaled_count;
    uint32_t send_credits;
    uint32_t receive_credits;
} NdsTransportQpState;

/* Complete transport descriptor. Both addresses point to contiguous arrays
 * in the backend's execution domain. Queue selection indexes both arrays. */
typedef struct NdsTransportDescriptor {
    uint64_t qp_descriptors_address;
    uint64_t qp_states_address;
    uint32_t qp_count;
    uint32_t reserved;
} NdsTransportDescriptor;

#if defined(__CCE_AICORE__)
#define NDS_TRANSPORT_INLINE __aicore__ inline
#define NDS_TRANSPORT_GLOBAL __gm__
#else
#define NDS_TRANSPORT_INLINE inline
#define NDS_TRANSPORT_GLOBAL
#endif

NDS_TRANSPORT_INLINE const NdsQpDescriptor *nds_transport_qp(
    NDS_TRANSPORT_GLOBAL const NdsTransportDescriptor *transport, uint32_t queue_index) {
    if (transport == 0 || queue_index >= transport->qp_count || transport->qp_descriptors_address == 0U)
        return 0;
    return (const NdsQpDescriptor *)(uintptr_t)(transport->qp_descriptors_address) + queue_index;
}

NDS_TRANSPORT_INLINE NdsTransportQpState *nds_transport_qp_state(
    NDS_TRANSPORT_GLOBAL const NdsTransportDescriptor *transport, uint32_t queue_index) {
    if (transport == 0 || queue_index >= transport->qp_count || transport->qp_states_address == 0U)
        return 0;
    return (NdsTransportQpState *)(uintptr_t)(transport->qp_states_address) + queue_index;
}

NDS_TRANSPORT_INLINE uint32_t nds_transport_should_signal(NDS_TRANSPORT_GLOBAL const NdsTransportQpState *state) {
    if (state == 0 || state->signal_interval == 0U)
        return 0U;
    return state->unsignaled_count + 1U >= state->signal_interval ? 1U : 0U;
}

NDS_TRANSPORT_INLINE uint32_t nds_transport_record_send(NDS_TRANSPORT_GLOBAL NdsTransportQpState *state,
                                                        uint32_t signaled) {
    if (state == 0 || state->send_credits == 0U)
        return 0U;
    --state->send_credits;
    state->unsignaled_count = signaled != 0U ? 0U : state->unsignaled_count + 1U;
    return 1U;
}

NDS_TRANSPORT_INLINE uint32_t nds_transport_record_receive(NDS_TRANSPORT_GLOBAL NdsTransportQpState *state) {
    if (state == 0 || state->receive_credits == 0U)
        return 0U;
    --state->receive_credits;
    return 1U;
}

NDS_TRANSPORT_INLINE void nds_transport_reclaim_send(NDS_TRANSPORT_GLOBAL NdsTransportQpState *state,
                                                     uint32_t completion_count) {
    if (state == 0 || state->signal_interval == 0U)
        return;
    state->send_credits += completion_count * state->signal_interval;
}

NDS_TRANSPORT_INLINE void nds_transport_reclaim_receive(NDS_TRANSPORT_GLOBAL NdsTransportQpState *state,
                                                        uint32_t completion_count) {
    if (state != 0)
        state->receive_credits += completion_count;
}

#if defined(__CCE_AICORE__)
__aicore__ __gm__ inline const NdsQpDescriptor *nds_transport_qp_global(__gm__ const NdsTransportDescriptor *transport,
                                                                        uint32_t queue_index) {
    if (transport == 0 || queue_index >= transport->qp_count || transport->qp_descriptors_address == 0U)
        return 0;
    return (__gm__ const NdsQpDescriptor *)(uintptr_t)(transport->qp_descriptors_address) + queue_index;
}

__aicore__ __gm__ inline NdsTransportQpState *nds_transport_qp_state_global(
    __gm__ const NdsTransportDescriptor *transport, uint32_t queue_index) {
    if (transport == 0 || queue_index >= transport->qp_count || transport->qp_states_address == 0U)
        return 0;
    return (__gm__ NdsTransportQpState *)(uintptr_t)(transport->qp_states_address) + queue_index;
}
#endif

#undef NDS_TRANSPORT_INLINE
#undef NDS_TRANSPORT_GLOBAL

/* Stateless transport-operation envelopes carry the complete transport and an
 * explicit queue selection. */
typedef struct NdsRdmaSendArgs {
    NdsTransportDescriptor transport;
    NdsSendWr wr;
    uint32_t queue_index;
    int32_t return_value;
} NdsRdmaSendArgs;

typedef struct NdsRdmaRecvArgs {
    NdsTransportDescriptor transport;
    NdsRecvWr wr;
    uint32_t queue_index;
    int32_t return_value;
} NdsRdmaRecvArgs;

typedef struct NdsRdmaReadArgs {
    NdsTransportDescriptor transport;
    NdsSendWr wr;
    uint32_t queue_index;
    int32_t return_value;
} NdsRdmaReadArgs;

typedef struct NdsRdmaWriteArgs {
    NdsTransportDescriptor transport;
    NdsSendWr wr;
    uint32_t queue_index;
    int32_t return_value;
} NdsRdmaWriteArgs;

#if defined(__cplusplus)
static_assert(sizeof(NdsTransportDescriptor) == 24, "device transport ABI changed");
static_assert(sizeof(NdsTransportQpState) == 16, "transport QP state ABI changed");
static_assert(sizeof(NdsRdmaSendArgs) == 80, "device RDMA send args ABI changed");
static_assert(sizeof(NdsRdmaRecvArgs) == 56, "device RDMA recv args ABI changed");
#else
_Static_assert(sizeof(NdsTransportDescriptor) == 24, "device transport ABI changed");
_Static_assert(sizeof(NdsTransportQpState) == 16, "transport QP state ABI changed");
_Static_assert(sizeof(NdsRdmaSendArgs) == 80, "device RDMA send args ABI changed");
_Static_assert(sizeof(NdsRdmaRecvArgs) == 56, "device RDMA recv args ABI changed");
#endif

#endif
