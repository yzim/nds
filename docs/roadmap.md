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
- TCP is bootstrap-only: it exchanges endpoint metadata, namespace capacity,
  and the session-static completion-record descriptor.
- RA, AIV, and standard-CP1 AICPU implement the NPU command post. The
  CPU-written NDS completion record remains the protocol completion in every
  mode.

## Delivered Work

- Defined endpoint-local ownership across runtime, transport, storage, and
  execution code; shared wire records remain NDS-owned.
- Implemented a memory-backed namespace with range validation, fixed command
  and completion records, serial Read/Write semantics, and one-command batch
  Read/Write with CPU-side whole-batch descriptor validation.
- Implemented CPU-initiated RDMA Read/Write data movement and terminal
  completion writes.
- Added a page-locked host client buffer for direct NPU RoCE data transfer
  through an RA-registered device-visible mapping.
- Implemented RA, AIV, and AICPU command paths, including device-callable
  transport and storage operations for AIV and AICPU.
- Unified RA, AIV, and AICPU on the same operation-specific storage command
  types and fixed-layout serializers; host-device invocation envelopes remain
  separate from client-server protocol data.
- Added native storage, paired lower-layer examples, persistent Torch storage,
  and opt-in bounded E2E coverage.

## Active Engineering Work

### Bootstrap Configuration

- Consolidate duplicated bootstrap configuration across native applications,
  examples, Torch sessions, and E2E runners.
- Keep private addresses, paths, and target-specific invocation under `.local/`
  or environment variables; tracked code must not acquire private topology.
- Preserve the NDS-owned TCP wire boundary and avoid exposing vendor handles or
  device-local queue data through configuration.

### Host-To-Device ABI

- Replace temporary execution accessors with explicit, value-like, non-owning
  RA/AIV/AICPU execution-resource views.
- Keep host C++ objects and HCCP/provider handles out of device-visible records.
- Version and validate NDS device records; make transfer ownership, alignment,
  and lifetime explicit before changing queue geometry or launch behavior.

### Runtime Loaders

- Consolidate duplicated CANN, runtime, RA, and device-discovery loader code
  behind the smallest practical NDS runtime boundary.
- Preserve fail-closed required-symbol resolution, one-release-per-process
  loading, private ABI structs, and the CPU executable's CANN-free boundary.
- Do not introduce direct vendor-library linkage or widen the public NDS API.

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
   completion demultiplexing, per-command timeouts, and error handling. Define
   client transport-local CQ ownership and error handling: normal AI-QPs
   currently let HCCP consume CQs, while `Transport` does not poll them.
2. Define QP selection, whether command and data QPs separate, and the
   synchronization required by concurrent device submitters.
3. Add multiple in-flight commands and multiple QPs only with a bounded test
   matrix and explicit resource-lifetime rules.
4. Evaluate an optional two-sided completion mode in which the NPU pre-posts
   Receives and polls its receive CQ for CPU completion Sends. This is not
   required for the current serial completion-record path.

## Later Scope

Persistence, a block-device namespace backend, narrower per-command memory
access policy, and a versioned public SDK are deferred until the serial protocol
and concurrency contracts are stable.
