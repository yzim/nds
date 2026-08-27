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

`src/include/nds/wire/transport.hh` owns the fixed QP-bootstrap wire record.
`src/common/transport.cc` implements shared QP identity, TCP bootstrap, and MTU
policy; `src/include/nds/tcp_bootstrap.hh` is its C++ interface.
`src/include/nds/storage_protocol.hh` owns the four semantic storage command
types and their fixed-layout, endian-explicit serializers and deserializers.
They operate on ordinary byte buffers and are the same functions used by the
host CPU, AICPU, and AIV. Backend and transport code do not depend on storage
command semantics.

`Runtime` owns AscendCL, network-service lifecycle, NPU allocation/copy, and
memory services. `Transport` borrows the runtime and owns an `Endpoint`, a
bounded indexed QP set, peer metadata, one TCP channel, and required per-QP
AI-QP WR-ID storage. `qp_count == 1` retains the original fixed-record
bootstrap. A larger count exchanges one framed batch of NDS-owned QP records
on the same TCP channel and connects QPs by index.
`Endpoint` owns the RA lifecycle and rdev. `StorageClient` borrows the runtime
and transport, owns protocol buffers and command sequencing, and registers
them through the endpoint.

The CPU server independently owns its verbs context, PD, and one CQ/RC-QP pair
per transport index. Applications own configuration, workload buffers, and
verification. Paired examples each exercise one lower API layer; storage
remains the complete application under `examples/storage/`.

The source layout reflects these boundaries:

```text
src/client/resource/    NPU lifecycle, transport, storage session
src/client/backend/     RA, AIV, and AICPU client backends
src/server/             CPU protocol, transport, and verbs backend
src/common/             shared transport implementation
src/torch/              reusable PyTorch extension
```

## Wire boundary and resources

The TCP bootstrap carries only versioned NDS records:

- Endpoint: QPN, PSN, GID, GID index, port, QoS/retry values, and diagnostic
  MTU. Multiple endpoint records are framed by a bounded count and paired by
  index; they still share one TCP connection.
- Storage bootstrap: completion-record address, length, rkey, and access.
- Namespace: CPU memory-backed capacity.
- Command: command ID, operation, namespace range, and NPU application-memory
  descriptor. Batch commands instead reference an NPU-resident array of
  fixed-width storage descriptors governed by the enclosing command version.

It never carries HCCP QP or MR handles, AI-QP descriptors, queue or doorbell
addresses, or provider objects. Those are local to the environment that owns
them.

The NPU creates one offline HCCP rdev and one or more RC QPs. Its lifecycle is:

```text
aclInit -> aclrtSetDevice -> rtOpenNetService(--hdcType=18)
        -> RaInit -> RaRdevInitV2
```

The offline path uses `NETWORK_OFFLINE`, `NOTIFY (1)`, and an enabled Lite
context. The selected backend mode determines QP creation:

| Mode | Creation | QP mode | Purpose |
|---|---|---|---|
| `ra` | `RaTypicalQpCreate` | OPBASE (`2`) | RA returns doorbell information. |
| `aiv` | `RaAiQpCreate` | OPBASE_EXT (`4`) by default | Caller-owned work queues; HCCP-owned CQs. |
| `aicpu` | `RaAiQpCreate` | NORMAL (`0`) | CP1 provider-owned Send path; HCCP-owned CQs. |

The CPU creates one independent RC QP for each NPU QP and moves each through
`INIT`, `RTR`, and `RTS`. Its active-port MTU determines `IBV_QP_PATH_MTU`; the
peer MTU is diagnostic because the CANN 9.0.0 `TypicalQp` ABI has no NPU
path-MTU field.

The CPU registers its command Receive record, namespace, and completion source
buffer. The NPU independently registers application, command, and completion
allocations. An application MR permits CPU remote read and write; commands
carry its address, rkey, length, and access direction. Client application data
may be allocated in NPU memory or as page-locked host memory. The latter uses
AscendCL `aclrtHostRegister`/`aclrtHostUnregister` around an NDS-owned host
allocation. NDS retains the host address for application copies and registers
the returned device-visible mapping through RA with `DirectNpu`; both remain
alive until the storage completion record is observed and its MR is
deregistered. `aclrtMallocHost` is not suitable because Ascend documents that
its result cannot be used in the device.

AI-QPs omit `NDS_RA_AI_CALLER_POLLS_CQ` by default. HCCP owns send and receive
CQ consumption; NDS does not interpret HCCP CQ activity as storage completion.
The client `Transport` layer does not poll a CQ or provide synchronous
transport completion. The current serial storage path uses QP zero and waits
for the CPU-written protocol completion record. Transport multiplicity does
not add command scheduling, CQ ownership, or concurrent storage requests;
those remain later design work.
The explicit NDS `PollCq(is_send_cq)` operation remains available only for a
future caller-owned-CQ configuration that opts into that RA flag. Its selector
matches CANN RA `RaPollCq`: `true` selects the send CQ and `false` the receive CQ.
Its successful result is the number of completions copied to the supplied
output, from zero through the requested limit. Device launch envelopes keep
that functional output in their `return_value` field: post operations report
zero or a negative normalized error, and PollCq reports its WC count or a
negative normalized error. Provider diagnostics are not part of the device
verbs API; a future extended or debug interface may define them separately.

Resources remain valid through the HCCP-managed local completion path, the CPU
data movement and terminal completion Write, and NPU observation of the
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

`StorageClient::read`, `write`, and their batch variants submit a command and
return a `StorageCompletionHandle`. `StorageClient::wait(handle, timeout_ms)`
observes the CPU-written `StorageCompletion` record and validates its command
ID, status, and byte count. It never depends on a client CQ. One client permits
one live handle: it retains the command resources, application MRs, and batch
descriptor allocation until the terminal record is observed. A timeout leaves
the handle live, so those resources remain valid and the caller can wait again.
A matching terminal failure releases the handle's resources before `wait`
returns that failure.

`StorageClient::read_batch` and `StorageClient::write_batch` each submit one
cross-endpoint command. Its `BATCH_READ` or `BATCH_WRITE` record identifies an
NPU-registered descriptor array and total payload bytes; the CPU RDMA Reads the
array, validates every descriptor and the aggregate length, then performs its
ordered payload operations before one terminal completion Write. A malformed
batch has no namespace effect. The batch itself remains one command in flight;
it does not introduce command pipelining.

The CPU polls its verbs CQ for command Receive and terminal completion Write.
The CPU-written NDS completion record is the storage completion. An RA local
CQE, HCCP AI-QP CQ activity, ACL synchronization, provider resolution, or an
operator launch is not NDS storage completion. `StorageClient::wait` observes
the record through bounded device-to-host copies for RA. AIV and AICPU launch
their separate device wait operator, which deserializes the same record in
device code; the host stream timeout bounds that operator but is not the
storage completion signal.

The four command semantics are explicit across backend environments:

| Operation | Shared semantic command | RA input | AIV/AICPU invocation |
|---|---|---|---|
| Read | `StorageReadCommand` | `RaStorageContext` plus command | `NdsDeviceStorageReadArgs` |
| Write | `StorageWriteCommand` | `RaStorageContext` plus command | `NdsDeviceStorageWriteArgs` |
| Batch Read | `StorageBatchReadCommand` | `RaStorageContext` plus command | `NdsDeviceStorageBatchReadArgs` |
| Batch Write | `StorageBatchWriteCommand` | `RaStorageContext` plus command | `NdsDeviceStorageBatchWriteArgs` |

The invocation arguments are host-device ABI envelopes, not network records.
AIV copies command fields between global and local memory outside serde; serde
itself has no backend-mode branches.

## Backend modes

Backend mode decides where a work request runs, not which API layer is used.
All modes share host lifecycle, QP/MR setup, TCP bootstrap, and CPU protocol
execution.

| Mode | RDMA-post site | Host launch role | Local completion |
|---|---|---|---|
| `ra` | Host CPU: `NdsRaPostSend` calls `RaTypicalSendWr`, then `rtRDMADBSend` | Executes the post and submission | RA CQ available |
| `aiv` | NPU AIV writes WQEs and rings a doorbell | Creates and launches the operation-specific device args | HCCP-owned SCQ/RCQ by default; caller-owned polling is opt-in |
| `aicpu` | Standard CP1 provider posts Send | Creates and launches the operation-specific device args | HCCP-owned SCQ/RCQ by default; caller-owned polling is opt-in |

### Submission

`PostSend` is post-and-submit on every supported backend. It returns success
only after the work request has been submitted to its transport path; it does
not expose a deferred-doorbell option.

`NdsAivPostSendBatch` is an AIV-only transport entrypoint for one contiguous,
device-global `NdsDeviceSendWr` array. Its launch envelope carries the array
address and count. The array remains valid until the AIV launch completes. The
entrypoint traverses the array in order, populating and publishing each valid
WQE. It advances the producer once and rings one doorbell for the successfully
posted prefix. On an invalid WR or exhausted SQ, it stops at that element and
reports the error after ringing the prefix, following `ibv_post_send` partial
post behavior. This entrypoint has the same single-producer-per-QP requirement
as `NdsAivPostSend`. On an error, `bad_wr_address` identifies the first
unposted array element, equivalent to `ibv_post_send`'s `bad_wr`; it is zero
on success.

For RA, `RaTypicalSendWr` prepares a WQE and returns `{dbIndex, dbInfo}`. The
RA interface has no doorbell-ring call, so NDS immediately invokes the CANN
runtime `rtRDMADBSend` on the selected runtime stream. This follows HCOMM's
normal OPBASE send path. HCOMM also batches several prepared WQEs before one
runtime doorbell when its algorithm explicitly chooses that policy.

AIV directly writes its WQE and doorbell in `PostSend`. AICPU uses the
standard-CP1 provider's `ibv_exp_post_send`, whose documented ABI has no
deferred-doorbell control. Do not introduce a common deferred-submission API
until a measured bottleneck justifies it and a documented AICPU mechanism can
support its contract.

### RA

RA is the smallest hardware-specific surface and the first mode to use for
validation. It uses an OPBASE Lite QP. `RaTypicalSendWr` supplies doorbell
information; NDS then invokes dynamically resolved `rtRDMADBSend` after
selecting the logical device.

### AIV

AIV operates on an NDS-owned device QP and connection, never an HCCP handle or
host C++ object. The device ABI under `src/client/include/nds/` describes QP,
work request, CQ, connection, and host-launch records. AIV exposes device
Send, Receive, Read, Write, and an optional caller-owned `PollCq(is_send_cq)` API. CANN 9.0.0 requires its loadable
image to be one CCEC translation unit, though standalone objects remain for
compile and symbol verification.

`NdsDevicePostSendArgs`, `NdsDevicePostRecvArgs`, and `NdsDevicePollCqArgs`
are operation-launch envelopes for AIV and AICPU. Their payload fields retain
the verbs-shaped `SendWr`, `RecvWr`, and completion-output contract; they do
not define a second verbs request layer. Every envelope is allocated in
device-visible memory and embeds its functional `return_value`, so its result
is read back with the envelope rather than through CANN-owned launch-argument
memory.

### AICPU

AICPU is an NDS-built standard-CP1 package registered through ACL CPU-kernel
mode `0`; this means package registration, not a process named CP0. CP1
dynamically resolves `libhns-rdmav25.so:ibv_exp_post_send` within the device
environment. The optional caller-owned CQ poll uses provider symbols when
exported and the NDS queue-address fallback otherwise. The host process must never load this
provider. NDS intentionally has no custom-process AICPU mode because CANN does
not publish the required RNIC mapping/import contract.

Both AIV and AICPU launch through `aclrtLaunchKernelWithHostArgs`, passing one
host-side `uint64_t` containing the device-global envelope address. The AIV
`GM_ADDR` entrypoint consumes that scalar directly; the AICPU entrypoint
decodes the scalar from CANN's host-argument storage before accessing the same
device-resident envelope. The shared launch API is therefore input-only; the
envelope's embedded result is copied back explicitly after synchronization.

## Runtime ABI boundary

Stable platform dependencies, including the NPU client's public AscendCL API
and CPU-only `libibverbs`, link normally. Version-coupled `libra.so` and
`libruntime.so` stay behind CANN runtime loaders. All CANN libraries in one NPU
process must come from one release. Loaders resolve required symbols before
hardware work and fail closed with useful diagnostics; private ABI structs stay
inside the loader boundary. Before an NPU-client process uses a dynamically
loaded CANN or driver library, its matching CANN `<cann-root>/set_env.sh` must
be sourced; loaders use the CANN-provided library search path and never embed
an installation-specific absolute library path. CANN-free unit and integration
builds omit the NPU-client targets.

NDS does not load or wrap HCOMM or HCCL. HCOMM's bundled transport is reference
material only because it requires matching peer logic, flag buffers, and
communicator synchronization not present in the CPU verbs server.

## Related work

The protocol delivery state and future multi-command, multi-QP, and two-sided
completion work are in [the roadmap](roadmap.md). The public sources that
inform lifecycle and ABI decisions are in [references](references.md).
