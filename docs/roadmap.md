# Roadmap

NDS is evolving from a bounded RoCE interoperability harness into a
storage-oriented protocol between an NPU initiator and a CPU target. This page
records the protocol direction and distinguishes the implemented single-command
baseline from later concurrency work.

## Target Model

The NPU sends storage commands to the CPU with RDMA Send. The CPU receives a
command, executes it against a memory-backed namespace, and reports completion
to the NPU through a completion flag. The CPU performs the data movement:

- A storage Write causes the CPU to RDMA Read data from NPU-advertised memory.
- A storage Read causes the CPU to RDMA Write namespace data to
  NPU-advertised memory.

The NPU does not issue RDMA Read or RDMA Write for storage data. The initial
CPU namespace is an in-memory byte-addressable region. Commands carry an
offset and length; persistence and a block-device backend are later work.

The CPU configures namespace capacity at startup and advertises that capacity
to the NPU during bootstrap. The NPU rejects an out-of-range request before it
sends the command. The CPU repeats the authoritative range check before it
accesses namespace memory and returns a range error through the completion
record. Both checks are required. The initial memory-backed namespace is
zero-initialized, so a Read of an unwritten range has defined contents.

TCP is bootstrap-only: it exchanges QP metadata, CPU namespace capacity, and
the session-static NPU completion-flag MR descriptor. It does not carry storage
commands, statuses, or payload data. The CPU does not send protocol requests
to the NPU.

The implementation is organized by responsibility:

1. Backend: host RA, AIV, and AICPU own NPU-specific Send posting and their
   local completion behavior.
2. Transport: NPU and CPU connections own QPs, MRs, buffers, and CQ handling,
   but expose connection-level operations rather than QP/CQ internals.
3. Protocol: defines session bootstrap, command records, data-placement
   descriptors, responses, sequencing, and errors.
4. Application: chooses workloads and validates transferred data. It is not a
   protocol or transport responsibility.

The current source boundary is deliberately small: `src/common` owns protocol
records and bootstrap; `src/npu_client/modes` owns backend-specific posting;
`src/npu_client/transport/storage_submission.cc` dispatches one registered
command record to a backend; and
`src/cpu_server/transport/storage_execution.cc` owns CPU data movement and
terminal completion publication. The command-line endpoints remain applications
that create a session and perform the bounded verification workload.

The selected NPU backend posts the protocol command with RDMA Send. The CPU
uses ordinary `libibverbs` for its command Receive, storage-data RDMA
Read/Write, completion-flag RDMA Write, and explicit CQ polling. CPU completion
ownership is therefore uniform across NPU backends.

## Completion Model

The first revision uses a one-sided completion flag, modeled after HCOMM's
transport flag flow. Each command advertises an NPU-registered completion
record. After receiving the command and completing its CPU-initiated RDMA Read
or Write, the CPU RDMA Writes the completion record into that NPU memory. The
NPU waits for the record to reach its requested terminal value before reusing
the command or data buffer.

This deliberately avoids an NPU Receive WR and receive-CQ dependency in the
first storage path. It is not an RNIC CQ completion: CPU must still actively
poll its send CQ before writing the terminal completion flag, and the NPU must
not treat backend launch or HCCP background processing as command completion.
The NPU backend contract must state how NDS safely observes the completion
record for Host RA, AIV, and AICPU.

NDS owns separate internal NPU allocations and MRs for command records and
completion records. The completion-flag MR descriptor is exchanged once during
TCP bootstrap. Application data is supplied separately and registered as its
own MR; protocol code must not partition or take ownership of an application's
data allocation. Each storage command carries that application's data-MR
descriptor, as NVMe-oF carries a per-command data descriptor.

For the first revision, an application data MR permits both CPU remote read
and CPU remote write, in addition to local write. This lets one registration
serve storage Write and Read. A later policy interface may narrow access by
command or application.

The registered command buffer contains one fixed-size record:

```text
request_id, operation (Read or Write), namespace offset, length,
NPU data address, NPU data rkey
```

The completion record contains `request_id`, terminal state, protocol status,
and `bytes_transferred`. The CPU posts data and then its terminal completion
flag Write in that order on the same RC QP. Normal success relies on RC QP
ordering, as in HCOMM's flag flow: the NPU cannot observe the terminal record
before preceding data movement. A missing terminal record is a timeout or
transport failure, not success.

The CPU uses one verbs CQ for the initial QP, pre-posts one command Receive WR
before the NPU can submit, and actively polls that CQ for command receive and
CPU RDMA send completion. The NPU initially waits for the completion record
with a bounded backend-appropriate device-memory poll and stream/device
synchronization before host inspection. AIV and AICPU must not pretend that an
HCCP-managed AI-QP CQ is an NDS completion API.

## Delivery Order

- [x] Introduce the backend, transport, protocol, and application boundaries.
- [x] Implement a memory-backed CPU namespace with offset-and-length Read and
  Write commands.
- [x] Implement NPU Send and CPU Receive for command and completion records.
- [x] Implement CPU-initiated RDMA Read for storage Write data movement.
- [x] Implement CPU-initiated RDMA Write for storage Read data movement.
- [x] Define the initial project-facing completion contract for each NPU backend.
- [x] Implement and validate the common storage protocol contract with Host RA
  first.
- [x] Implement and validate that same storage protocol contract for AICPU and
  AIV. All three backends are required; Host RA is only the implementation
  starting point.
- [x] Validate a Read of an untouched range and a deterministic storage Write.
- [ ] Add a multi-command application workflow that writes a deterministic
  payload and reads it back in the same session.
- [ ] Add an optional two-sided completion mode: NPU pre-posts Receive WRs and
  actively polls its receive CQ for CPU completion Send records. This is the
  NVMe-oF-like direction for scalable command queues, but is not required for
  the initial one-command storage path.

## Concurrency Roadmap

The first protocol revision permits exactly one outstanding command on one RC
QP for each NPU-to-CPU session. That QP carries NPU Send commands, CPU receive
WRs, CPU-initiated RDMA Read/Write data movement, and CPU completion-flag
Writes. NDS owns one fixed-size registered NPU command record; the application
data buffer capacity bounds a command's transfer length. This is intentional:
it proves command ordering, receive ownership, memory lifetime, and teardown
before adding queue management.

Later revisions must support multiple in-flight commands and multiple RDMA QPs
within one NPU-to-CPU session. They may separate command and data QPs or stripe
data across a selected QP set. That requires explicit command identifiers,
queue depth and credit negotiation, receive-WR replenishment, completion
demultiplexing, a registered command queue, per-command timeout/error handling,
and QP selection policy.
Those features must be designed as protocol and transport behavior, not added
as application-side parallel loops.
