# HCCP QP and MR Lifecycle

This guide describes the common resource model used before any NDS execution
mode posts a request. It covers the NPU HCCP/RA rdev, QP, and memory
registration; the independent CPU verbs resources; the NDS transport bootstrap;
and teardown. Mode-specific posting and CQ handling are described in
[NPU execution modes](npu-backends.md).

In the source tree, HCCP lifecycle code is shared implementation support under
`src/client/backend/support`; Host RA posting remains under
`src/client/backend/host_ra`, while AIV and AICPU code lives under
`src/client/device`. `Transport` uses those resources without exposing HCCP
handles to `StorageClient`.

## Ownership model

The NPU process owns one HCCP rdev, one HCCP QP, and separate registered NPU
application, command, and completion allocations. The CPU process independently
owns its verbs context, PD, CQ, QP, command Receive record, memory namespace,
and completion-record source buffer. The TCP transport bootstrap never
transfers an HCCP or verbs object between processes.

NDS exchanges only versioned NDS records:

- Endpoint record: QPN, PSN, GID, GID index, port, QoS/retry values, and a
  diagnostic MTU.
- Storage bootstrap: NPU completion-record address, length, rkey, and access.
- Namespace record: CPU memory-backed namespace capacity.
- Command record: request ID, operation, namespace range, and NPU application
  memory descriptor.

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
| `aiv` | `RaAiQpCreate` | Configurable; OPBASE_EXT (`4`) by default | HCCP returns caller-owned SQ/RQ/SCQ/RCQ dataplane data. |
| `aicpu` | `RaAiQpCreate` | Configurable; NORMAL (`0`) by default | CP1 probes provider symbols first and uses the same dataplane record for fallbacks. |

For AI QPs, NDS supplies an RC QP shape with one SGE per work request and
applies traffic class, service level, retry timeout, and retry count after
creation. With caller CQ polling enabled, HCCP returns provider addresses and
SQ/RQ/SCQ/RCQ dataplane information. NDS converts these into one versioned
descriptor containing queue geometry, producer/consumer state, and
record/MMIO doorbell addresses. It remains local to the NPU environment.

## Peer connection

After QP creation, `RaGetQpAttr` returns the NPU QPN, PSN, GID, and GID index.
NDS serializes those public peer fields into its endpoint record through
`src/common/transport.*`. The CPU server creates an independent RC QP using
`libibverbs`, moves it through `INIT`, `RTR`, and `RTS`, and returns its own
endpoint record.

The NPU converts the CPU endpoint record into the RA representation and calls
`RaTypicalQpModify`. The CPU uses its local active-port MTU for
`IBV_QP_PATH_MTU`. The NDS peer MTU is diagnostic only because the CANN 9.0.0
`TypicalQp` ABI has no NPU-side path-MTU field.

## Memory registration and keys

The CPU registers a command Receive record, memory-backed namespace, and
completion-record source buffer. The NPU separately registers application,
command, and completion allocations. The application MR grants CPU remote read
and remote write; the command MR supplies the NPU Send SGE key; and the
completion MR grants CPU remote write and is exchanged once during TCP
bootstrap. Commands carry the application address, rkey, length, and access
direction.

## Lifetime and teardown

Posting a command does not permit either endpoint to release its resources.
The QPs, MRs, queue WR-ID sidecars, and NPU allocations remain valid until the
NPU consumes its signaled send CQE, the CPU completes its data and terminal
completion Write, and the NPU observes that completion record.

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
[runtime libraries and ABI](runtime-abi.md) for the library boundary.
