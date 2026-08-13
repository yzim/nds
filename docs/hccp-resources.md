# HCCP QP and MR Lifecycle

This guide describes the common resource model used before any NDS submission
mode posts a request. It covers the NPU HCCP/RA rdev, QP, and memory
registration; the independent CPU verbs resources; the NDS wire exchange; and
teardown. Mode-specific posting and CQ handling are described in
[submission modes](modes.md).

## Ownership model

The NPU process owns one HCCP rdev, one HCCP QP, its NPU device allocation, and
the HCCP MR for that allocation. The CPU process independently owns its verbs
context, PD, CQ, QP, host buffer, and verbs MR. The TCP peer exchange never
transfers an HCCP or verbs object between processes.

NDS exchanges only versioned NDS records:

- Endpoint record: QPN, PSN, GID, GID index, port, QoS/retry values, and a
  diagnostic MTU.
- CPU memory record: destination address, length, rkey, remote-write access,
  and transaction ID.

It must not exchange HCCP QP or MR handles, AI-QP descriptors, queue or
doorbell addresses, or provider-private objects. Those addresses are valid only
in the execution environment that owns them.

## NPU rdev and QP

`NpuRaContext` initializes AscendCL, the runtime network service, and RA once:

```text
aclInit -> aclrtSetDevice -> rtOpenNetService(--hdcType=18) -> RaInit
```

`NpuRaQp` creates the rdev with `RaRdevInitV2` using the selected physical NPU
and NPU RNIC IPv4 address. NDS uses `NETWORK_OFFLINE`, `NOTIFY (1)`, and an
enabled Lite context (`disabled_lite_thread=false`). `NO_USE (0)` is not valid
for the offline rdev lifecycle used by this path.

NDS creates exactly one RC QP on that rdev. The selected mode determines the
HCCP creation entry point and requested QP mode:

| Mode | HCCP creation | Requested QP mode | Resource purpose |
|---|---|---|---|
| `host-ra` | `RaTypicalQpCreate` | OPBASE (`2`) | Host RA post returns runtime doorbell information. |
| `aiv` | `RaAiQpCreate` | OPBASE_EXT (`4`) | HCCP returns AI send-WQ data used by the AIV kernel. |
| `aicpu` | `RaAiQpCreate` | NORMAL (`0`) | Standard CP1 provider post rings the normal-QP doorbell. |

For AI QPs, NDS supplies an RC QP shape with one SGE per work request and
applies traffic class, service level, retry timeout, and retry count after
creation. HCCP returns AI-QP information. AICPU uses its opaque AI-QP address;
AIV uses its send-WQ queue and doorbell descriptors. These remain local to the
NPU execution environment.

## Peer connection

After QP creation, `RaGetQpAttr` returns the NPU QPN, PSN, GID, and GID index.
NDS serializes those public peer fields into its endpoint record. The CPU server
creates an independent RC QP using `libibverbs`, moves it through `INIT`, `RTR`,
and `RTS`, and returns its own endpoint record.

The NPU converts the CPU endpoint record into the RA representation and calls
`RaTypicalQpModify`. The CPU uses its local active-port MTU for
`IBV_QP_PATH_MTU`. The NDS peer MTU is diagnostic only because the CANN 9.0.0
`TypicalQp` ABI has no NPU-side path-MTU field.

## Memory registration and keys

The CPU allocates its destination buffer and registers it with `ibv_reg_mr`
using `IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE`. It sends the payload
address, length, and rkey to the NPU in the NDS memory record.

The NPU allocates source memory through AscendCL, fills it with the test payload,
and registers it through `RaRegisterMr` on the HCCP rdev. NDS supplies the NPU
device address, length, and `NDS_RA_ACCESS_DIRECT_NPU`. RA returns an opaque MR
handle and keys:

- The NPU lkey identifies the local source SGE to the NPU RNIC.
- The CPU rkey authorizes the NPU RNIC to write the CPU destination MR.
- The NPU rkey is not sent in the current one-way NPU-to-CPU Write because the
  CPU does not initiate an RDMA operation.

Read and Send need different CPU-side resource setup and are not end-to-end
features yet.

## Lifetime and teardown

Submission does not permit either endpoint to release its resources. The QPs,
MRs, and NPU allocation remain valid through the mode's completion handling.
The current CPU payload/guard exchange is an integration-test harness used to
hold resources while the bounded Write is checked; it is not the final
project-facing completion interface.

NDS tears down in reverse ownership order:

```text
RaDeregisterMr -> free NPU allocation -> RaQpDestroy
-> RaRdevDeinit(NOTIFY) -> RaDeinit -> rtCloseNetService -> aclFinalize
```

The CPU deregisters its verbs MR and releases its QP, CQ, PD, and context as
the CPU server resources leave scope.

## Scope and references

The resource lifecycle is based on the CANN 9.0.0 HCCP implementation in the
matching HCOMM source tree, particularly `src/platform/hccp`, and is implemented
by `NpuRaContext` and `NpuRaQp`. NDS dynamically loads the required RA/runtime
ABI and does not link or copy HCCP implementation code. See
[linkage and runtime ABI](linkage.md) for the library boundary.
