# Architecture

NDS has two top-level endpoints: an NPU client and a CPU server. Each endpoint
uses four layers with one dependency direction:

```text
Application -> Storage protocol -> Transport connection -> Backend
```

## Source Tree

```text
src/
  client/         NPU-attached endpoint
    main.cc        CLI, application buffers, and workload verification
    protocol.*     NPU side of the storage command and completion flow
    transport.*    one connected NPU-to-CPU transport session
    backend/       host_ra, aiv, aicpu, and their shared RA support
  server/         CPU-side endpoint
    main.cc        CLI and memory-backed namespace ownership
    protocol.*     command decode, range checks, data movement, completion
    transport.*    one connected CPU-to-NPU transport session
    backend.*      libibverbs QP, MR, work request, and CQ operations
  common/
    transport.*    endpoint metadata, TCP bootstrap, and MTU policy
    protocol.*     shared versioned storage records and codecs
    logging.*      replaceable process logging
```

## Backend

Backends own resource and work-request mechanics. The NPU backend creates and
registers resources through the dynamically loaded CANN RA boundary, then
posts Send through exactly one selected implementation: `host_ra`, `aiv`, or
`aicpu`. RA contexts, QPs, and runtime loaders are implementation support under
`client/backend/support`; they are not a fourth mode.

The CPU backend owns `libibverbs` context, PD, CQ, RC QP, MR registration,
Receive posting, RDMA Read/Write posting, and CQ polling. Backend interfaces do
not encode storage commands or make namespace decisions.

## Transport

`transport.hh` defines each endpoint's `Connection`. A connection
owns the backend resources, one RC QP, registered-region lifetimes, and the raw
TCP bootstrap session. It exposes endpoint-appropriate operations: the NPU
sends a registered buffer; the CPU receives, reads, and writes registered
buffers. Protocol records and storage access flags do not belong here.

One QP per connection is an initial constraint, not the final model. A future
connection may own multiple QPs with command and data roles without changing
application ownership.

## Storage Protocol

The protocol layer owns the storage flow. It encodes and decodes bootstrap,
namespace, command, and completion records; validates command ranges; sends the
command; selects CPU RDMA Read for storage Write or CPU RDMA Write for storage
Read; and publishes the terminal completion record. TCP carries only session
bootstrap records. The command, payload, and completion use the RoCE path.

## Application

Applications own process configuration, namespace or data buffers, request
selection, and workload verification. They call the storage protocol and do
not manipulate QPs, MRs, work requests, CQs, or wire codecs.

## Shared Boundaries

`src/common/transport.*` contains shared endpoint metadata, TCP bootstrap, and
MTU policy. `src/common/protocol.*` contains the C-compatible storage record
ABI and codecs. Neither endpoint exchanges HCCP handles, verbs objects, AI-QP
descriptors, queue addresses, or doorbell addresses.

The corresponding headers live in `src/common/include/nds/`. They are shared
between NDS targets, not an installed public SDK: `transport.h` is the C ABI
for endpoint metadata and MTU policy, `transport.hh` owns the C++ TCP
bootstrap object, and `protocol.h` is the C ABI for storage records.
