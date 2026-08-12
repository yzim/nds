# Submission Modes

NDS provides three ways to submit an RDMA Write from one Ascend NPU RNIC to a
plain CPU `libibverbs` RC QP. They share control-plane exchange, RA rdev
lifecycle, QP connection, NPU memory registration, CPU destination-memory
registration, and teardown. They differ in who constructs the WQE, who rings
the SQ doorbell, and how completion is observed.

| Mode | Submission / post path | RA QP creation mode | NPU send-CQ consumer |
|---|---|---|---|
| `host-ra` | NPU-side host CPU calls `RaTypicalSendWr`, then `rtRDMADBSend` | OPBASE (`2`) through `RaTypicalQpCreate` | NPU-side host CPU calls `RaPollCq` |
| `aiv` | NDS AIV code writes the SQ and hardware doorbell directly | OPBASE_EXT (`4`), normalized by HCCP to provider OP (`2`) | HCCP internally handles the AI-QP CQ on the NPU; NDS does not call `RaPollCq` |
| `aicpu` | Standard CP1 AICPU calls HNS provider `ibv_exp_post_send`, which rings the normal-QP doorbell | NORMAL (`0`) through `RaAiQpCreate` | HCCP internally handles the AI-QP CQ on the NPU; NDS does not call `RaPollCq` |

The modes are selected with `nds_npu_qp_client --submission-mode`. The CPU
endpoint is always `nds_verbs_server` using ordinary `libibverbs`; it does not
load CANN, HCCP, HCOMM, or HCCL.

The CPU peer's payload and guard verification is not an NPU CQ poll. It is an
internal state of the current integration-test server: it holds resources while
the bounded Write harness checks the result. It is not the project-facing
completion interface. AIV and AICPU currently rely on HCCP's internal AI-QP CQ
handling, and defining how NDS exposes or integrates their completion remains
unfinished work.

Detailed guides:

- [Host RA](host-ra.md)
- [AIV](aiv.md)
- [AICPU](aicpu.md)

## Common lifecycle

All modes begin with the same direct one-NPU lifecycle. HCCP/RA owns the NPU
rdev and QP; the CPU process owns its verbs context, PD, CQ, QP, and MR.

```text
aclInit -> aclrtSetDevice -> rtOpenNetService(--hdcType=18) -> RaInit
  -> RaRdevInitV2(NETWORK_OFFLINE, NOTIFY)
  -> create local RC QP
  -> exchange QPN, PSN, GID, retry, and destination-MR metadata over TCP
  -> RaTypicalQpModify
  -> allocate and register NPU source memory
  -> mode-specific submission
  -> receive CPU transfer verification
  -> deregister MR and destroy QP/rdev
  -> RaDeinit -> rtCloseNetService -> aclFinalize
```

## QP setup through HCCP

`NpuRaContext` initializes AscendCL, the runtime network service, and RA once.
`NpuRaQp` then creates one rdev with `RaRdevInitV2` using the selected physical
NPU, the NPU RNIC IPv4 address, `NETWORK_OFFLINE`, and `NOTIFY (1)`. The Lite
context remains enabled (`disabled_lite_thread=false`), matching the validated
CANN 9.0.0 device-RoCE path. `NO_USE (0)` is not valid for this offline rdev
lifecycle.

NDS creates exactly one RC QP on that rdev. The selected submission mode fixes
the HCCP QP creation call and mode:

| Mode | HCCP creation | Requested QP mode | Why |
|---|---|---|---|
| `host-ra` | `RaTypicalQpCreate` | OPBASE (`2`) | HCCP returns host-submittable doorbell data. |
| `aiv` | `RaAiQpCreate` | OPBASE_EXT (`4`) | HCCP exposes the AI send-WQ descriptor for AIV SQ and doorbell writes. |
| `aicpu` | `RaAiQpCreate` | NORMAL (`0`) | The standard-CP1 HNS provider post path rings the normal-QP doorbell itself. |

For AI QPs, NDS supplies an RC QP shape with one SGE per work request, applies
traffic class/service level, retry timeout, and retry count after creation, and
retains the returned AI-QP information. That information contains the opaque
AI-QP address used by AICPU; for AIV it also contains the send-WQ queue and
doorbell descriptors. These fields are local execution resources, not TCP
control-plane data and not portable host pointers.

After creation, `RaGetQpAttr` returns the local QPN, PSN, GID, and GID index.
NDS places those fields, plus port, retry, QoS, and diagnostic MTU values, in
its versioned endpoint record. The CPU creates its own RC QP with `libibverbs`
and returns the same peer-facing fields. Each side converts the received record
to its local QP representation: the NPU calls `RaTypicalQpModify`; the CPU
moves its verbs QP through `INIT`, `RTR`, and `RTS`. Neither endpoint sends an
HCCP handle, verbs object, AI-QP descriptor, or provider-private object over
TCP.

The CPU selects its `IBV_QP_PATH_MTU` from its local active port. HCCP
`TypicalQp` has no corresponding NPU MTU field, so the peer MTU record remains
diagnostic and does not negotiate or clamp the CPU QP MTU.

## Memory registration and keys

After both QPs are connected, the CPU allocates a guarded host buffer and
registers it with `ibv_reg_mr` using `IBV_ACCESS_LOCAL_WRITE` and
`IBV_ACCESS_REMOTE_WRITE`. It sends an NDS memory descriptor containing the
payload address, length, rkey, remote-write access flag, and transaction ID.
The NPU validates that descriptor before it allocates device memory.

The NPU fills the deterministic payload in host memory, copies it into an ACL
device allocation, and calls `RaRegisterMr` on the HCCP rdev. NDS passes the
device address, length, and `NDS_RA_ACCESS_DIRECT_NPU`; RA returns an opaque MR
handle plus a local key and remote key. For the current NPU-to-CPU Write, the
submission request uses the NPU MR local key and the CPU descriptor rkey. The
NPU rkey is not sent because the CPU does not initiate an RDMA operation.

Both MRs, both QPs, and the NPU device allocation remain alive after a request
is submitted. Host RA waits for its CQE; AI modes wait for the CPU's
transaction acknowledgment because HCCP owns their AI CQ. Only after CPU
verification does NDS deregister the NPU MR with `RaDeregisterMr`, free device
memory, destroy the QP, deinitialize the rdev with `NOTIFY`, and tear down RA.
The CPU deregisters its verbs MR as its server resources leave scope.

The NDS TCP control plane is not an HCOMM synchronization protocol. Its final
transaction acknowledgment is an internal test-harness state that keeps the QP
and MRs alive until the CPU has checked the bounded Write. This is not a
substitute for an NPU completion API. ACL kernel synchronization is not RNIC
completion, and HCCP owns the AI-QP CQs.

## Choosing a mode

Use `host-ra` first. It has the smallest hardware-specific surface and gives
the NPU process an explicit send CQE through `RaPollCq`.

Use `aiv` when the request must be submitted by an Ascend vector core and the
application accepts ownership of the CANN-matched HNS WQE and doorbell layout.

Use `aicpu` when submission must occur on AICPU but should remain a one-WR
provider operation. It avoids manual WQE encoding, but requires a standard CP1
customer-kernel installation and the CANN-matched HNS provider.

These are implementation choices, not performance rankings. NDS has validated
correctness and bounded repeated AIV submission; it has not published a
comparative throughput or latency benchmark.

## Current feature boundary

- End-to-end CPU-peer validation currently exercises RDMA Write.
- The CPU payload/guard acknowledgment exists only for the bounded integration
  test. It is not the final completion interface.
- Host RA exposes an NDS-owned send-CQ poll. AIV and AICPU need a defined
  project-facing completion contract above or alongside HCCP's internal AI-CQ
  handling.
- AIV currently implements Write only.
- The AICPU request ABI represents Write, Read, and Send, and its kernel builds
  all three WR forms. Read and Send still need matching CPU-side memory/receive
  setup and end-to-end validation.
- No mode initializes HCOMM/HCCL, consumes a rank table, or requires a second
  NPU.
