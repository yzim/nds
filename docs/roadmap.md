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

1. RMA: Host RA, AIV, and AICPU submit opcodes on a selected protocol resource and
   own their local completion capabilities.
2. Transport: owns connected QPs, MRs, buffers, and peer metadata and exposes
   Send and one-sided data operations.
3. StorageClient: owns storage bootstrap, command records, request sequencing,
   namespace bounds, and protocol completion.
4. Application: chooses workloads and validates transferred data.

The current source tree implements these boundaries independently in
`src/client` and `src/server`. The NPU-attached client has `storage.*`,
`transport.*`, `rma.*`, and a `main.cc` application entry point. Its
execution-specific work-request implementations remain under
`client/execution/`.
The server keeps its protocol, transport, and verbs implementation. Shared
QP identity and TCP bootstrap live in `src/common/connection.*`, and
shared storage records live in `src/common/protocol.*`.
See [architecture](architecture.md) for concrete ownership rules.

`src/include/nds/` is an internal shared-header boundary. Its `nds/`
prefix prevents generic names such as `protocol.h` and `connection.h` from
colliding across targets. It is not yet an installed or versioned external
NDS SDK; any public-library surface will be designed separately when NDS needs
one.

The selected NPU execution mode posts the protocol command with RDMA Send. The CPU
uses ordinary `libibverbs` for its command Receive, storage-data RDMA
Read/Write, completion-flag RDMA Write, and explicit CQ polling. CPU completion
ownership is therefore uniform across NPU execution modes.

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
not treat operator launch or HCCP background processing as command completion.
Each storage implementation must state how it safely observes the completion
record in its execution environment.

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
CPU RDMA send completion. The current host StorageClient observes the NPU
completion allocation through a bounded device-to-host copy loop. Future AIV
and AICPU StorageClient implementations must poll the record in device memory
with correct cache and ordering semantics. They must not pretend that an
HCCP-managed AI-QP CQ is an NDS completion API.

## Delivery Order

- [x] Introduce the work-request, transport, StorageClient, and application
  boundaries.
- [x] Implement a memory-backed CPU namespace with offset-and-length Read and
  Write commands.
- [x] Implement NPU Send and CPU Receive for command and completion records.
- [x] Implement CPU-initiated RDMA Read for storage Write data movement.
- [x] Implement CPU-initiated RDMA Write for storage Read data movement.
- [x] Define the initial project-facing completion contract for each NPU
  execution mode.
- [x] Implement and validate the common storage protocol contract with Host RA
  first.
- [x] Validate the host StorageClient with AIV and AICPU command-Send RMA paths.
- [ ] Implement device-callable AIV and AICPU Transport APIs.
- [x] Add device-callable AIV and AICPU storage Read and Write operators.
- [ ] Switch the host validation executable to launch those storage operators.
- [x] Validate a deterministic storage Write with Host RA, AIV, and AICPU.
- [x] Validate a Host RA Read of an untouched range.
- [ ] Validate an AIV and AICPU Read of an untouched range.
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
