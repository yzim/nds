# Design

NDS interoperates between one Ascend NPU RNIC and one CPU-side RoCE RNIC. The
NPU client dynamically loads the narrow CANN RA boundary; the CPU server uses
ordinary `libibverbs`. The production path does not initialize HCOMM, HCCL,
TSD, a rank table, or a second NPU.

The direct verbs implementation and paired example currently define the
settled lower-layer correctness baseline. Their error-propagation contract and
performance benchmark are not complete. The transport layer owns the request,
completion, queue-credit, and multi-QP contracts used by the bounded storage
pipeline.

## Architecture and ownership

The endpoint-local dependency direction is:

```text
Application -> StorageClient -> Transport -> Verbs -> protocol resources
```

`src/include/tcp_socket.hh` owns TCP connection and listener lifecycles.
`src/include/transport_protocol.hh` owns the fixed transport records and
their serializers; `src/common/transport_protocol.cc` implements them and MTU
policy. Client and server transport own the ordering of those records.
`src/include/storage_protocol.hh` owns the four semantic storage command
types and their fixed-layout, endian-explicit serializers and deserializers.
They operate on ordinary byte buffers and are the same functions used by the
host CPU, AICPU, and AIV. Backend and transport code do not depend on storage
command semantics.

`Runtime` owns AscendCL, network-service lifecycle, NPU allocation/copy, and
memory services. The client `Transport` borrows the runtime and owns an
`Endpoint`, a bounded indexed QP set, peer metadata, one TCP-backed peer
channel, and the backend-specific complete `NdsTransportDescriptor` descriptor.
It opens the channel first,
sends a `TransportInfo` QP-count request, and creates QPs only after the
server replies with the accepted count. The endpoints then exchange one fixed
`TransportInfo` record containing the ordered QP descriptors and connect QPs
by index.
The client `Endpoint` owns the RA lifecycle and rdev. The server `Endpoint`
owns one CPU verbs context, protection domain, QP, and registered-memory
objects; server `Transport` owns the negotiated indexed set of those QPs and
the TCP bootstrap. `StorageClient` borrows the runtime
and transport, owns protocol buffers and command sequencing, and registers
them through the endpoint.

The CPU `TransportListener` owns the TCP listening socket and accepts one
connection at a time. Each accepted connection creates an independent
`Transport` session with its own verbs context, PD, and one CQ/RC-QP pair per
accepted index. Applications own configuration, workload buffers, and
verification. Paired examples each exercise one lower API layer; storage
remains the complete application under `examples/storage/`.

The source layout reflects these boundaries:

```text
src/client/             NPU lifecycle, transport, and storage session
src/client/loaders/     dynamic CANN, RA, and DSMI ABI loaders
src/client/backends/    RA, AIV, and AICPU client backends
src/server/             CPU protocol, endpoint, and multi-QP transport
src/common/             shared transport implementation
src/torch/              reusable PyTorch extension
```

The RA backend is also built as the NDS-owned `nds_ra_backend` shared artifact.
It packages the RA verbs, transport, and storage layers behind C entrypoints;
`RaLauncher` resolves the complete entrypoint set at runtime. The artifact is
separate from the vendor `libra.so` loader and does not replace that loader.

## Wire boundary and resources

The TCP connection carries exact bytes and has no framing or typed operations.
The transport protocol serializes these versioned `TransportInfo` records on
that connection:

- QP count: the client requests one through sixteen QPs before either endpoint
  creates QPs. The server replies with the lesser of that request and its
  configured per-client maximum. The accepted count applies only to that TCP
  session.
- Endpoint: QPN, PSN, GID, GID index, port, QoS/retry values, and diagnostic
  MTU. The fixed record holds up to eight descriptors, paired by index.

It never carries HCCP QP or MR handles, AI-QP descriptors, queue or doorbell
addresses, or provider objects. Those are local to the environment that owns
them.

After QP zero is connected, RDMA carries the storage protocol records:

- Storage bootstrap: the NPU sends a fixed record containing the completion
  descriptor and a registered namespace-response descriptor. The CPU pre-posts
  one Receive for this record before publishing its QP identity.
- Namespace: the CPU serializes capacity and RDMA Writes that record to the
  NPU-advertised namespace-response descriptor. The client observes the record
  through bounded memory copies; it does not use a client receive CQ.
- Command: command ID, operation, namespace range, and NPU application-memory
  descriptor. Batch commands instead reference an NPU-resident array of
  fixed-width storage descriptors governed by the enclosing command version.

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
| `aiv` | `RaAiQpCreate` | OPBASE_EXT (`4`) by default | Caller-owned work queues; CQ ownership follows `QueuePairCallerPollsCq`. |
| `aicpu` | `RaAiQpCreate` | NORMAL (`0`) | AICPU provider-owned Send path; CQ ownership follows `QueuePairCallerPollsCq`. |

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

AI-QPs are created with `NDS_RA_AI_CALLER_POLLS_CQ` when the client transport
backend owns CQ reclamation. Without that flag, HCCP owns CQ consumption; NDS
does not interpret HCCP CQ activity as storage completion. The client
`Transport` layer does not expose CQ polling to transport callers: its backend
polls internally when transport credit reclamation requires it.
Storage bootstrap uses QP zero and waits for the CPU-written protocol
completion record. After bootstrap, each negotiated QP has a bounded storage
slot window and one worker on the CPU endpoint. The current client allocates
sixteen slots per QP; every slot descriptor carries its explicit QP index, and
`StorageClient` can keep one live completion handle per slot. The slot index
selects both the command/completion buffers and the matching transport QP. The
CPU pre-posts the active receive window, processes completions in QP order, and
reposts each slot buffer after its command has been handled. A listener may
serve multiple sessions serially, with each session's QPs and verbs resources
torn down before the next is accepted.
The explicit NDS `PollCq(is_send_cq)` operation is available when the caller
opts an AI-QP into caller-owned CQ mode with that RA flag. Its selector
matches CANN RA `RaPollCq`: `true` selects the send CQ and `false` the receive CQ.
Its successful result is the number of completions copied to the supplied
output, from zero through the requested limit. Device launch envelopes keep
that functional output in their `return_value` field: post operations report
zero or a negative normalized error, and PollCq reports its WC count or a
negative normalized error. Provider diagnostics are not part of the device
verbs API; a future extended or debug interface may define them separately.

`NdsTransportDescriptor` is the complete multi-QP device descriptor. Its
`qp_descriptors_address` and `qp_states_address` fields point to parallel
arrays, and a queue index selects the corresponding entries in both arrays.
`NdsQpDescriptor` remains the hardware-facing descriptor; `NdsTransportQpState`
contains transport-owned scheduling fields that do not belong to a QP, namely
the fixed signal interval, unsignaled count, and send/receive credit values.
The arrays are owned by `Transport` through `MemoryBuffer`: RA uses host
buffers, while AIV and AICPU use device buffers. The initial credit values
mirror the negotiated/configured queue capacities. Transport backends update
these fields as they accept WQEs and reclaim CQEs.

Resources remain valid through the selected local CQ completion path, the CPU
data movement and terminal completion Write, and NPU observation of the
completion record. Teardown is reverse ownership order:

```text
RaDeregisterMr -> free NPU allocation -> RaQpDestroy
-> RaRdevDeinit(NOTIFY) -> RaDeinit -> rtCloseNetService -> aclFinalize
```

## Storage protocol and completion

Each connected RC QP permits one storage command per available slot in its
bounded slot window. A storage Write causes the CPU to RDMA Read NPU
application data into its namespace; a storage Read causes the CPU to RDMA
Write namespace data to that buffer. The CPU posts the data operation and
terminal completion Write in order on the command's QP.

Storage session setup is a QP-zero control exchange. The CPU posts the
bootstrap Receive before it publishes its local QP identity. The NPU sends the
expanded `StorageBootstrap` record only after the RC QP is connected. The CPU
validates that record and RDMA Writes `StorageNamespace` capacity to the
client's registered response memory. The client waits for that record before it
permits storage submission. TCP has no storage-protocol payload after QP setup.

`StorageClient` is control-path only. It owns the bootstrap resources, storage
slot descriptors, the per-slot state array, and host-side slot reservation
bookkeeping. The application calls `allocate_slot()` (optionally selecting a
queue), registers its application buffer through `register_memory()`, and
passes the returned address and key to the stateless launcher. It calls
`release_slot()` only after completion has been observed.

The launcher exposes `storage_read`, `storage_write`, their batch variants,
and a separate `storage_wait` operator. The operation arguments contain only
the complete storage descriptor, packed queue/slot ID, server offset, client
buffer address/key, and a 32-bit transfer length. Command IDs, slot metadata,
and expected byte counts are allocated and maintained internally in the
per-slot state. `storage_wait` observes the CPU-written `StorageCompletion`
record and validates its command ID, status, and byte count. It never depends
on a client CQ. A timeout leaves the slot active so the caller can wait again.

Launcher batch operators each submit one cross-endpoint command. Its
`BATCH_READ` or `BATCH_WRITE` record identifies an
NPU-registered descriptor array and total payload bytes; the CPU RDMA Reads the
array, validates every descriptor and the aggregate length, then performs its
ordered payload operations before one terminal completion Write. A malformed
batch has no namespace effect. The batch itself remains one command in flight
within its slot; other slots on the same QP may carry independent commands.

The CPU polls its verbs CQ for command Receive and terminal completion Write.
The CPU-written NDS completion record is the storage completion. An RA local
CQE, HCCP AI-QP CQ activity, ACL synchronization, provider resolution, or an
operator launch is not NDS storage completion. The host launcher observes the
record through bounded probes. Direct AIV and AICPU device integrations may
use their separate wait entrypoints to probe the same record in device code;
the host stream timeout bounds the probes but is not the storage completion
signal.

The four command semantics are explicit across backend environments:

| Operation | Shared semantic command | Launcher arguments |
|---|---|---|
| Read | `StorageReadCommand` | `NdsStorageOperationArgs` |
| Write | `StorageWriteCommand` | `NdsStorageOperationArgs` |
| Batch Read | `StorageBatchReadCommand` | `NdsStorageBatchOperationArgs` |
| Batch Write | `StorageBatchWriteCommand` | `NdsStorageBatchOperationArgs` |

The invocation arguments are host-device ABI envelopes, not network records.
The RA, AIV, and AICPU adapters construct the shared protocol command from
these fields; serde itself has no backend-mode branches.

## Backend modes

Backend mode decides where a work request runs, not which API layer is used.
All modes share host lifecycle, QP/MR setup, TCP bootstrap, and CPU protocol
execution.

| Mode | RDMA-post site | Host launch role | Local completion |
|---|---|---|---|
| `ra` | Host CPU: `NdsRaPostSend` calls `RaTypicalSendWr`, then `rtRDMADBSend` | Executes the post and submission | RA CQ available |
| `aiv` | NPU AIV writes WQEs and rings a doorbell | Creates and launches the operation-specific device args | Transport-owned SCQ/RCQ reclamation |
| `aicpu` | AICPU provider posts Send; shared metadata posts Recv | Creates and launches the operation-specific device args | Transport-owned SCQ/RCQ reclamation |

### Submission

The client transport API owns control-path setup, memory registration, QP state,
and descriptor export. It does not submit data-path work. Callers construct
NDS-owned `NdsSendWr` or `NdsRecvWr` records and invoke the
stateless `Launcher::rdma_send`, `rdma_recv`, `rdma_read`, or `rdma_write`
operation with an explicit `LaunchConfig`, the complete `NdsTransportDescriptor`,
and a queue index. Storage records and backend/device WR layouts are not part
of the transport control API. The caller owns its buffers, registered regions,
and WR IDs. The transport ignores the caller's `NDS_SEND_SIGNALED` bit and
applies its per-QP signal interval; transport callers must leave that bit clear.

The launcher also has an operation-specific send-batch API, but batching is still
a bounded transport capability rather than a general asynchronous scheduler.
The AIV path currently accepts multi-request send/read/write batches; RA and
AICPU accept only one request for those operations. A one-request batch is
therefore portable, while multi-request batches are explicitly AIV-only.
Receive batches are not part of the client control API. The AIV batch path
applies signaling independently of the batch boundary and rings once after its
successfully posted prefix. A signal interval can therefore span multiple
public calls and batches; a batch boundary has no completion meaning.

The direct verbs launcher API still provides explicit CQ polling over a host QP
descriptor for verbs-layer users. Transport callers use only the stateless
RDMA APIs; the selected backend polls and interprets CQEs internally to
replenish transport credits. For AI QPs, the transport exports a contiguous
device descriptor array and leaves host-only opaque handles unset in that
device copy. The RA path instead exports the host descriptor array and does not
allocate a device descriptor buffer. Storage completion remains a separate
protocol-visible completion-slot state.

The CPU verbs transport applies the same receive lifecycle: each successful
post consumes one per-QP receive credit and assigns a transport-owned WR ID;
`wait_receive` rejects a wait without a pending post, preserves the pending
post across a timeout, and retires it after any observed CQE. CPU send, RDMA
read, and RDMA write retirement also validate their expected WR IDs. The CPU
queue depth is fixed by the backend's configured QP capacity, and this
credit accounting is separate from storage-command scheduling.

`LaunchConfig::sync_timeout_ms` bounds backend launch synchronization. Its
default remains five seconds and it is an internal launch setting; it is not a
CLI option. CQ reclamation policy belongs to the selected transport backend.

Every accepted transport call rings its newly posted WQE or successfully
posted prefix. Signaling is scheduled by the fixed per-QP interval independently
of public-call and batch boundaries. When a signal is scheduled, the backend
uses the corresponding CQE to reclaim the completed WQE window; callers do not
poll the transport CQ. This local transport completion is still distinct from
peer storage completion.

The AIV path also accepts a contiguous device-global `NdsSendWr` array
for a multi-request send/read/write batch. The launcher copies that array to
device memory and invokes the AIV batch entrypoint, which applies the ongoing
per-QP signal schedule and rings one doorbell for the successfully posted
prefix. The result reports the prefix length and the first unposted request. It
has the same single-producer-per-QP requirement as a single post. RA and AICPU
currently retain single-request entrypoints.

The client `register_memory` API creates a move-only `MemoryRegion`; the caller
keeps that region alive through the selected launcher operation and its CQ
completion. The CPU verbs endpoint configures sixteen incoming and outgoing RDMA-read
credits on each QP, matching the bounded AIV batch limit. This is a transport
capacity setting; it is separate from storage-command completion and does not
make batches a general asynchronous scheduler.

For RA, `RaTypicalSendWr` prepares a WQE and returns `{dbIndex, dbInfo}`. The
RA interface has no doorbell-ring call, so NDS immediately invokes the CANN
runtime `rtRDMADBSend` on the selected runtime stream. This follows HCOMM's
normal OPBASE send path. HCOMM also batches several prepared WQEs before one
runtime doorbell when its algorithm explicitly chooses that policy.

AIV directly writes its WQE and doorbell in `PostSend`. AICPU uses the
AICPU provider's `ibv_exp_post_send` for Send, while its Recv and caller-owned
CQ paths write the shared HNS queue metadata directly because `ibv_post_recv`
and `ibv_poll_cq` are inline libibverbs wrappers rather than exported symbols.
Do not introduce a common deferred-submission API until a measured bottleneck
justifies it and a documented AICPU mechanism can support its contract.

### RA

RA is the smallest hardware-specific surface and the first mode to use for
validation. It uses an OPBASE Lite QP. `RaTypicalSendWr` supplies doorbell
information; NDS then invokes dynamically resolved `rtRDMADBSend` after
selecting the logical device.

### AIV

AIV operates on an NDS-owned device QP and connection, never an HCCP handle or
host C++ object. The device ABI under `src/client/include/` describes QP,
work request, CQ, connection, and host-launch records. AIV exposes device
Send, Receive, Read, Write, and an optional caller-owned `PollCq(is_send_cq)` API. CANN 9.0.0 requires its loadable
image to be one CCEC translation unit, though standalone objects remain for
compile and symbol verification.

`NdsPostSendArgs`, `NdsPostRecvArgs`, and `NdsPollCqArgs`
are operation-launch envelopes for AIV and AICPU. Their payload fields retain
the verbs-shaped `SendWr`, `RecvWr`, and completion-output contract; they do
not define a second verbs request layer. Every envelope is allocated in
device-visible memory and embeds its functional `return_value`, so its result
is read back with the envelope rather than through CANN-owned launch-argument
memory.

### AICPU

NDS follows HCOMM's AICPU deployment model. The backend uses
`ACL_RT_BINARY_LOAD_OPT_CPU_KERNEL_MODE` with value `0`, and its operator JSON
manifest and kernel archive are installed in the selected CANN root under
`opp/vendors/nds`. The archive is registered in that CANN root's
`ascend_package_load.ini`.

This is an implementation decision based on target validation, not merely a
packaging preference. On node200, the CANN-root mode-0 package deployment
completed the AI-QP operation. Two alternatives failed: Bisheng's
compiler-embedded `<<< >>>` loading path, and a package loaded from outside
the CANN package tree. The latter could load and submit an operator, but AICPU
execution failed before the QP operation completed. NDS therefore does not use
either alternative for this backend.

`scripts/install_aicpu_package.sh` installs or removes exactly the NDS vendor
files and registration stanza. Normal users do not need a mount-namespace
overlay.

The kernels use the required AICPU memory-sharing permission and dynamically resolve
`libhns-rdmav25.so` only inside the AICPU device environment. The host process
must never load that provider. Permission is a deployment concern independent
of the one-pointer kernel ABI. The envelope's embedded result is copied back
explicitly after synchronization.

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
