# Roadmap

NDS is evolving from a bounded RoCE interoperability harness into a
storage-oriented protocol between one NPU initiator and one CPU target. This
page records work that changes the project; [design](design.md) owns the
current architecture and protocol contract.

## Current Baseline

- One connected RC QP carries one command at a time. Storage submission returns
  one completion handle, and a protocol-record wait resolves it.
- The NPU sends storage commands; the CPU uses `libibverbs` to receive them,
  move data, and write the terminal completion record.
- A storage Write makes the CPU RDMA Read NPU-advertised data into its
  memory-backed namespace; a storage Read makes the CPU RDMA Write namespace
  data back to NPU memory or a page-locked host client buffer.
- TCP is bootstrap-only: it first negotiates the client-requested QP count and
  then exchanges endpoint metadata. After QP zero connects, RDMA exchanges the
  storage bootstrap and returns namespace capacity through a CPU RDMA Write to
  client-advertised response memory. The CPU listener may serve serial client
  sessions; it creates a fresh endpoint-local QP set only after accepting a
  session's count.
- RA and AIV implement the NPU command post. The AICPU backend follows HCOMM's
  CANN-root, CPU-kernel mode-0 package deployment path. The
  CPU-written NDS completion record remains the protocol completion in every
  mode.

## Delivered Work

- Defined endpoint-local ownership across runtime, transport, storage, and
  backend code; shared wire records remain NDS-owned.
- Implemented a memory-backed namespace with range validation, fixed command
  and completion records, serial Read/Write semantics, and one-command batch
  Read/Write with CPU-side whole-batch descriptor validation.
- Implemented CPU-initiated RDMA Read/Write data movement and terminal
  completion writes.
- Added a page-locked host client buffer for direct NPU RoCE data transfer
  through an RA-registered device-visible mapping.
- Implemented RA, AIV, and AICPU command paths, including device-callable
  transport and storage operations for AIV and AICPU.
- Added an AIV-only batched Send post entrypoint that traverses a device-global
  WR array, then rings once for its successfully posted prefix.
- Unified RA, AIV, and AICPU on the same operation-specific storage command
  types and fixed-layout serializers; host-device invocation envelopes remain
  separate from client-server protocol data.
- Added native storage, paired lower-layer examples, persistent Torch storage,
  and opt-in bounded E2E coverage.
- Settled the verbs example and E2E contract: direct one-QP Send/Recv/PollCq
  and configured RDMA Write are tested for RA and AIV, while AICPU remains in
  the storage E2E matrix because its CANN-root NORMAL package leaves receive
  and CQ ownership in HCCP rather than exposing the synchronous verbs CQ path.

## Active Engineering Work

### Bootstrap Configuration

- Consolidate duplicated bootstrap configuration across native applications,
  examples, Torch sessions, and E2E runners.
- Keep private addresses, paths, and target-specific invocation under `.local/`
  or environment variables; tracked code must not acquire private topology.
- Preserve the NDS-owned TCP wire boundary and avoid exposing vendor handles or
  device-local queue data through configuration.

### Host-To-Device ABI

- Replace temporary backend accessors with explicit, value-like, non-owning
  RA/AIV/AICPU backend-resource views.
- Keep host C++ objects and HCCP/provider handles out of device-visible records.
- Keep fixed-layout assertions and semantic validation for NDS device records;
  make transfer ownership, alignment, and lifetime explicit before changing
  queue geometry or launch behavior.
- Keep the current device `*Args` ABI limited to standard functional results.
  Design provider/path diagnostics only as a later, explicitly versioned
  extended or debug interface; do not add diagnostic fields to the verbs-shaped
  launch envelopes.
- Evaluate a target-host AscendC `<<< >>>` AIV launch stub as an alternative to
  the current dynamically loaded host-argument path. Establish whether a stub
  can launch the separately loaded NDS AIV binary on the installed CANN
  release, preserve the narrow runtime boundary and resource lifetime rules,
  and provide a measured benefit before adopting it. Keep the AICPU package
  `void *args` ABI independent of that evaluation.

### Runtime Loaders

- Consolidate duplicated CANN, runtime, RA, and device-discovery loader code
  behind the smallest practical NDS runtime boundary.
- Preserve fail-closed required-symbol resolution, one-release-per-process
  loading, private ABI structs, and the CPU executable's CANN-free boundary.
- Link the mandatory public AscendCL API directly; do not introduce direct
  linkage for private runtime, RA, or provider interfaces or widen the public
  NDS API.

### Performance

- Define an opt-in target benchmark plan before optimizing: workload, payload
  sizes, warmup, iteration count, backend, topology, and success criteria.
- Measure end-to-end latency and throughput separately from control-path,
  device-post, CPU data-movement, and completion-observation costs.
- Compare changes only against recorded baselines on the same target and report
  correctness, resource use, and variance with the result. Do not represent a
  faster local step as end-to-end storage performance.

## Protocol Evolution

The initial serial session deliberately proves ordering, receive ownership,
memory lifetime, and teardown before queue management. Later work must be
designed as transport and protocol behavior, not application-side parallel
loops:

1. Define queue depth, credits, receive-WR replenishment, command IDs,
   completion demultiplexing, per-command timeouts, and error handling for
   concurrent storage commands. `Transport` already retires its own final-CQE
   windows in the submission path; storage needs a separate credit and
   completion-demultiplexing policy.
2. Define command/data-QP selection policy and the synchronization required by
   concurrent device submitters. Transport now creates and connects a bounded,
   index-paired QP set, but storage continues to use QP zero.
3. Add multiple in-flight commands only with a bounded test matrix and
   explicit resource-lifetime rules.
4. Evaluate an optional two-sided completion mode in which the NPU pre-posts
   Receives and polls its receive CQ for CPU completion Sends. This is not
   required for the current serial completion-record path.

## Later Scope

Persistence, a block-device namespace backend, narrower per-command memory
access policy, and a versioned public SDK are deferred until the serial protocol
and concurrency contracts are stable.
