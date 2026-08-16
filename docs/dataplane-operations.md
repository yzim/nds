# Device Data Plane

The NPU data plane is independent of the host RA/HCCP control path. The host
creates and connects the QP, registers memory, and packages device-visible
addresses. AIV or AICPU code then operates on an NDS-owned device QP and
connection without receiving an HCCP handle or host C++ object.

## Layers

```text
Storage API
    -> device transport/session protocol (future)
        -> device connection: RDMA Send, Recv, Read, Write
            -> device verbs: post_send, post_recv, poll_cq
                -> provider symbols or SQ/RQ/CQ and doorbells
```

The C-compatible shared ABI is split by responsibility:

- `device_qp.h`: `nds_device_qp`, SQ/RQ/SCQ/RCQ descriptors, provider QP/CQ
  addresses, doorbell modes, and WR-ID sidecars.
- `device_verbs.h`: SGE, send WR, receive WR, CQ-poll request, completion, and
  operation result types.
- `device_connection.h`: a versioned `nds_device_connection` containing one QP
  and the transfer description used by connection operations.
- `device_operations.h`: the narrow host-launch dispatch record. It is an
  adapter into the connection layer, not the device API itself.

These headers live in `src/client/device/include/nds/`. The QP and connection
records remain local to the NPU endpoint and are never exchanged with the CPU.

## Verbs APIs

AIV exposes the following AICore-callable functions in
`src/client/device/aiv/device/include/nds/aiv_device_api.h`:

```text
NdsAivPostSend(qp, send_wr, scratch, result)
NdsAivPostRecv(qp, recv_wr, scratch, result)
NdsAivPollCq(qp, poll_request, scratch, result)
```

Their implementation is in `src/client/device/aiv/device/nds_aiv_dataplane.cc`.
Another AIV operator includes the API header and compiles that implementation
into its AICore translation unit. CANN 9.0.0 rejects the otherwise valid
multi-object AIV image at ACL load time, so NDS deliberately builds the
loadable entry and dataplane as one translation unit. A standalone
`aiv/nds_aiv_dataplane.o` is still emitted for compile and symbol verification,
but is not presented as a separately loadable kernel.

AICPU exports these symbols from `libnds_aicpu_standard.so`:

```text
NdsAicpuPostSend(qp, send_wr, result)
NdsAicpuPostRecv(qp, recv_wr, result)
NdsAicpuPollCq(qp, poll_request, result)
```

`PostSend` resolves the installed HNS provider's `ibv_exp_post_send` inside
standard CP1. `PostRecv` and `PollCq` try provider/verbs symbols first and use
the NDS queue-address implementation when those inline verbs operations are
not exported.

## Connection APIs

The connection layer accepts `nds_device_connection` plus an
`nds_device_transfer` and constructs the appropriate verbs WR. Both execution
environments expose:

```text
RdmaSend(connection, transfer, result)
RdmaRecv(connection, transfer, result)
RdmaRead(connection, transfer, result)
RdmaWrite(connection, transfer, result)
```

The concrete symbols are prefixed `NdsAiv` or `NdsAicpu`. Send, Read, and
Write lower to `PostSend` with distinct logical WR opcodes. Recv lowers to
`PostRecv`. CQ polling intentionally remains a verbs operation because callers
must choose SCQ or RCQ and consume explicit work completions.

The loaded entry points `NdsAivConnectionOp` and `NdsAicpuConnectionOp` only
validate and dispatch the versioned host-launch record into these connection
or verbs functions. Other device operators do not need to call the entry
points.

RNIC work completions are not NDS storage completion. Storage completion
remains the terminal protocol record written by the CPU endpoint.
