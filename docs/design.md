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
bounded set of QPs, peer metadata, one TCP channel, and per-QP AI-QP WR-ID
storage. `qp_count == 1` retains the original single-record bootstrap. A
multi-QP transport exchanges one framed batch of NDS-owned QP records on that
same TCP channel, connects records by index, and exposes explicit indexed QP
selection. It does not expose provider queue objects or create one control
connection per QP.
`Endpoint` owns the RA lifecycle and rdev. `StorageClient` borrows the runtime
and transport, owns protocol buffers and command sequencing, and registers
them through the endpoint.

The CPU server independently owns its verbs context, PD, CQ, RC QP, command
Receive record, namespace, and completion-record source buffer. Applications
own configuration, workload buffers, and verification. Paired examples each
exercise one lower API layer; storage remains the complete application under
`apps/`.

The source layout reflects these boundaries:

```text
src/client/resource/    NPU lifecycle, transport, storage session
src/client/execution/   RA, AIV, and AICPU command execution
src/server/             CPU protocol, transport, and verbs backend
src/common/             shared transport implementation
src/torch/              reusable PyTorch extension
```

## Wire boundary and resources

The TCP bootstrap carries only versioned NDS records:

- Endpoint: QPN, PSN, GID, GID index, port, QoS/retry values, and diagnostic
  MTU.
- Storage bootstrap: completion-record address, length, rkey, and access.
- Namespace: CPU memory-backed capacity.
- Command: command ID, operation, namespace range, and NPU application-memory
  descriptor. Batch commands instead reference an NPU-resident array of
  fixed-width storage descriptors governed by the enclosing command version.

It never carries HCCP QP or MR handles, AI-QP descriptors, queue or doorbell
addresses, or provider objects. Those are local to the environment that owns
them.

The NPU creates one offline HCCP rdev and one RC QP. Its lifecycle is:

```text
aclInit -> aclrtSetDevice -> rtOpenNetService(--hdcType=18)
        -> RaInit -> RaRdevInitV2
```

The offline path uses `NETWORK_OFFLINE`, `NOTIFY (1)`, and an enabled Lite
context. QP mode is an HCCP provider submission-mode selector, separate from
the RC QP transport type and separate from whether code executes on AIV or
AICPU. In particular, an AIV kernel is not by itself an OPBASE workflow.

| Numeric mode | HCOMM name | HCOMM use on 910B/910_93 |
|---:|---|---|
| `0` | NORMAL | AIV direct RoCE explicitly selects this mode to bypass the STARS scheduling submission path. |
| `2` | OPBASE | Operator-based workflow QP on pre-extended platforms; HCOMM typical RA QPs use it. |
| `4` | OPBASE_EXT | Extended operator-based workflow QP on 910B/910_93; HCOMM selects it for device-NIC OPBASE workflows and when its AICPU mode is enabled. |

Here, “operator-based workflow” is HCOMM terminology for a communication task
submitted through its runtime/operator workflow. It does not mean an arbitrary
NDS AIV operator which directly writes a WQE. `NORMAL` does not mean a
non-RDMA QP: all three entries above remain RC RoCE QPs.

NDS currently creates these QPs:

| NDS execution mode | Creation | Current NDS default | HCOMM direct-NPU reference |
|---|---|---:|---:|
| `ra` | `RaTypicalQpCreate` | OPBASE (`2`) | OPBASE (`2`) |
| `aiv` | `RaAiQpCreate` | OPBASE_EXT (`4`) | NORMAL (`0`) |
| `aicpu` | `RaAiQpCreate` | NORMAL (`0`) | OPBASE_EXT (`4`) |

The AIV and AICPU defaults are reversed relative to the cited HCOMM paths.
This is an NDS compatibility gap, not an evidence-backed reason to reinterpret
the numeric modes. Do not change a production default without an explicitly
scoped target validation of creation, submission, CQ ownership, and teardown.
The NPU-peer AIV benchmark may select `NORMAL` explicitly for direct-RoCE
experiments; that override does not change the endpoint default.
The analogous AICPU `OPBASE_EXT` mode requires the provider-returned doorbell
descriptor to be submitted with `rtRDMADBSend`; changing only the numeric mode
does not progress. The benchmark has a correctness-only host-operator probe
that performs this sequence across an AICPU post, a host copy/ring, and an
AICPU poll. It is not a production transport path or a bandwidth comparison
with HCOMM, whose private dispatcher appends the doorbell task without those
round trips.

The CPU creates an independent RC QP and moves it through `INIT`, `RTR`, and
`RTS`. Its active-port MTU determines `IBV_QP_PATH_MTU`; the peer MTU is
diagnostic because the CANN 9.0.0 `TypicalQp` ABI has no NPU path-MTU field.

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
transport completion. This does not affect the current serial storage path,
which waits for the CPU-written protocol completion record, but it leaves
transport-local CQ error and credit ownership for later design.
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

The four command semantics are explicit across execution environments:

| Operation | Shared semantic command | RA input | AIV/AICPU invocation |
|---|---|---|---|
| Read | `StorageReadCommand` | `RaStorageContext` plus command | `NdsDeviceStorageReadArgs` |
| Write | `StorageWriteCommand` | `RaStorageContext` plus command | `NdsDeviceStorageWriteArgs` |
| Batch Read | `StorageBatchReadCommand` | `RaStorageContext` plus command | `NdsDeviceStorageBatchReadArgs` |
| Batch Write | `StorageBatchWriteCommand` | `RaStorageContext` plus command | `NdsDeviceStorageBatchWriteArgs` |

The invocation arguments are host-device ABI envelopes, not network records.
AIV copies command fields between global and local memory outside serde; serde
itself has no execution-mode branches.

## Execution modes

Execution mode decides where a work request runs, not which API layer is used.
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

For RA, `RaTypicalSendWr` prepares a WQE and returns `{dbIndex, dbInfo}`. The
RA interface has no doorbell-ring call, so NDS immediately invokes the CANN
runtime `rtRDMADBSend` on the selected runtime stream. This follows HCOMM's
normal OPBASE send path. HCOMM also batches several prepared WQEs before one
runtime doorbell when its algorithm explicitly chooses that policy.

AIV directly writes its WQE and doorbell in `PostSend`. Its device benchmark
can defer the doorbell for non-final WQEs and ring a final grouped WQE; this is
an AIV-specific benchmark mechanism, not a common backend contract. AICPU uses the
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
not define a second verbs request layer. Each envelope owns its functional
`return_value`; no separate device result allocation or provider-diagnostic
record is exposed by this API.

### AICPU

AICPU is an NDS-built standard-CP1 package registered through ACL CPU-kernel
mode `0`; this means package registration, not a process named CP0. CP1
dynamically resolves `libhns-rdmav25.so:ibv_exp_post_send` within the device
environment. The optional caller-owned CQ poll uses provider symbols when
exported and the NDS queue-address fallback otherwise. The host process must never load this
provider. NDS intentionally has no custom-process AICPU mode because CANN does
not publish the required RNIC mapping/import contract.
ACL execution contexts are thread-local on the target; an AICPU launcher and
its stream must be created and used by the same worker thread, after that
thread binds the logical device.

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
