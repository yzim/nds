# Design

NDS interoperates between one Ascend NPU RNIC and one CPU-side RoCE RNIC. The
NPU client dynamically loads the narrow CANN RA boundary; the CPU server uses
ordinary `libibverbs`. The production path does not initialize HCOMM, HCCL,
TSD, a rank table, or a second NPU.

## Architecture and ownership

The endpoint-local dependency direction is:

```text
Application -> StorageClient -> Transport -> Verbs -> protocol resources
```

`src/common/connection.*` owns shared QP identity, TCP bootstrap, and MTU
policy. `src/common/protocol.*` owns shared versioned storage records and
codecs. Backends and transport do not depend on storage command semantics.

`Runtime` owns AscendCL, network-service lifecycle, NPU allocation/copy, and
memory services. `Transport` borrows the runtime and owns an `Endpoint`, one
QP, peer metadata, the TCP channel, and required AI-QP WR-ID storage.
`Endpoint` owns the RA lifecycle and rdev. `StorageClient` borrows the runtime
and transport, owns protocol buffers and request sequencing, and registers
them through the endpoint.

The CPU server independently owns its verbs context, PD, CQ, RC QP, command
Receive record, namespace, and completion-record source buffer. Applications
own configuration, workload buffers, and verification. Paired examples each
exercise one lower API layer; storage remains the complete application under
`apps/`.

The source layout reflects these boundaries:

```text
src/client/resource/    NPU lifecycle, transport, storage session
src/client/execution/   RA, AIV, and AICPU request execution
src/server/             CPU protocol, transport, and verbs backend
src/common/             shared connection and storage wire records
src/torch/              reusable PyTorch extension
```

## Wire boundary and resources

The TCP bootstrap carries only versioned NDS records:

- Endpoint: QPN, PSN, GID, GID index, port, QoS/retry values, and diagnostic
  MTU.
- Storage bootstrap: completion-record address, length, rkey, and access.
- Namespace: CPU memory-backed capacity.
- Command: request ID, operation, namespace range, and NPU application-memory
  descriptor.

It never carries HCCP QP or MR handles, AI-QP descriptors, queue or doorbell
addresses, or provider objects. Those are local to the environment that owns
them.

The NPU creates one offline HCCP rdev and one RC QP. Its lifecycle is:

```text
aclInit -> aclrtSetDevice -> rtOpenNetService(--hdcType=18)
        -> RaInit -> RaRdevInitV2
```

The offline path uses `NETWORK_OFFLINE`, `NOTIFY (1)`, and an enabled Lite
context. The selected execution mode determines QP creation:

| Mode | Creation | QP mode | Purpose |
|---|---|---|---|
| `ra` | `RaTypicalQpCreate` | OPBASE (`2`) | RA returns doorbell information. |
| `aiv` | `RaAiQpCreate` | OPBASE_EXT (`4`) by default | Caller-owned queue and CQ data. |
| `aicpu` | `RaAiQpCreate` | NORMAL (`0`) | CP1 provider-owned Send path. |

The CPU creates an independent RC QP and moves it through `INIT`, `RTR`, and
`RTS`. Its active-port MTU determines `IBV_QP_PATH_MTU`; the peer MTU is
diagnostic because the CANN 9.0.0 `TypicalQp` ABI has no NPU path-MTU field.

The CPU registers its command Receive record, namespace, and completion source
buffer. The NPU independently registers application, command, and completion
allocations. An application MR permits CPU remote read and write; commands
carry its address, rkey, length, and access direction.

Resources remain valid until the NPU consumes its signaled send CQE, the CPU
finishes data movement and terminal completion Write, and the NPU observes the
completion record. Teardown is reverse ownership order:

```text
RaDeregisterMr -> free NPU allocation -> RaQpDestroy
-> RaRdevDeinit(NOTIFY) -> RaDeinit -> rtCloseNetService -> aclFinalize
```

## Storage protocol and completion

One connected RC QP permits one command in flight. A storage Write causes the
CPU to RDMA Read NPU application data into its namespace; a storage Read causes
the CPU to RDMA Write namespace data to that buffer. The CPU posts the data
operation and terminal completion Write in order on the same QP.

The CPU polls its verbs CQ for command Receive and terminal completion Write.
The CPU-written NDS completion record is the storage completion. An RA local
CQE, HCCP AI-QP CQ activity, ACL synchronization, provider resolution, or an
operator launch is not NDS storage completion. The current host
`StorageClient` observes the record through a bounded device-to-host copy;
device-side storage clients must eventually provide their own correct cache and
ordering behavior.

## Execution modes

Execution mode decides where a work request runs, not which API layer is used.
All modes share host lifecycle, QP/MR setup, TCP bootstrap, and CPU protocol
execution.

| Mode | RDMA-post site | Host request role | Local completion |
|---|---|---|---|
| `ra` | Host CPU: `RaTypicalSendWr`, then `rtRDMADBSend` | Executes the post | RA CQ available |
| `aiv` | NPU AIV writes WQEs and rings a doorbell | Creates and launches the device request | Caller-owned SCQ/RCQ |
| `aicpu` | Standard CP1 provider posts Send | Creates and launches the device request | Caller-owned SCQ/RCQ |

### RA

RA is the smallest hardware-specific surface and the first mode to use for
validation. It uses an OPBASE Lite QP. `RaTypicalSendWr` supplies doorbell
information; NDS then invokes dynamically resolved `rtRDMADBSend` after
selecting the logical device.

### AIV

AIV operates on an NDS-owned device QP and connection, never an HCCP handle or
host C++ object. The device ABI under `src/client/include/nds/` describes QP,
work request, CQ, connection, and host-launch records. AIV exposes device
Send, Receive, Read, Write, and CQ-poll APIs. CANN 9.0.0 requires its loadable
image to be one CCEC translation unit, though standalone objects remain for
compile and symbol verification.

### AICPU

AICPU is an NDS-built standard-CP1 package registered through ACL CPU-kernel
mode `0`; this means package registration, not a process named CP0. CP1
dynamically resolves `libhns-rdmav25.so:ibv_exp_post_send` within the device
environment. Receive and CQ poll use provider symbols when exported and the
NDS queue-address fallback otherwise. The host process must never load this
provider. NDS intentionally has no custom-process AICPU mode because CANN does
not publish the required RNIC mapping/import contract.

## Runtime ABI boundary

Stable platform dependencies and CPU-only `libibverbs` link normally.
Version-coupled `libra.so` and `libruntime.so` stay behind runtime loaders;
`libascendcl.so` loads dynamically by default, with an optional version-pinned
public ACL link mode. All CANN libraries in one NPU process must come from one
release. Loaders resolve required symbols before hardware work and fail closed
with useful diagnostics; ABI structs stay inside the loader boundary.

NDS does not load or wrap HCOMM or HCCL. HCOMM's bundled transport is reference
material only because it requires matching peer logic, flag buffers, and
communicator synchronization not present in the CPU verbs server.

## Related work

The protocol delivery state and future multi-command, multi-QP, and two-sided
completion work are in [the roadmap](roadmap.md). The public sources that
inform lifecycle and ABI decisions are in [references](references.md).
