# Architecture

NDS has two top-level endpoints: an NPU client and a CPU server. The NPU client
uses one dependency direction:

```text
Application -> StorageClient -> Transport -> Verbs -> protocol resources
```

## Source Tree

```text
src/
  client/         NPU-attached endpoint
    main.cc        CLI, application buffers, and workload verification
    include/      shared device-safe QP, WR, CQ, and connection records
    resource/     stateful RA/HCCP resources, storage session, and storage dispatch
    execution/    RA / AIV / AICPU execution backends
      ra/         host CPU verbs post and doorbell
      aiv/        AIV host launcher and device operators
        device/   AIV device kernels plus their local device headers
      aicpu/      AICPU host launcher and device operators
        device/   AICPU device kernels plus their local device headers
  server/         CPU-side endpoint
    main.cc        CLI and memory-backed namespace ownership
    protocol.*     command decode, range checks, data movement, completion
    transport.*    one connected CPU-to-NPU transport session
    backend.*      libibverbs QP, MR, work request, and CQ operations
  common/
    connection.*   QP identity, TCP bootstrap, and MTU policy
    protocol.*     shared versioned storage records and codecs
    logging.*      replaceable process logging
  include/        shared client/server header boundary
    nds/          protocol.h, connection.h, connection.hh, result.hh, logging.hh
```

## API Matrix

RA, AIV, and AICPU are execution modes. Verbs, transport, and
storage are API layers. They are separate axes:

| Layer | RA | AIV | AICPU |
|---|---|---|---|
| Verbs | `NdsRaPostSend`, `NdsRaPostRecv`, `NdsRaPollCq` | `PostSend`, `PostRecv`, `PollCq` over device QP addresses | Exported provider/fallback `PostSend`, `PostRecv`, `PollCq` |
| Transport operations | RA execution view selects a QP and invokes verbs | Device `Transport` selects a device QP | Device `Transport` selects a device QP |
| Transport control | Host `Transport` creates and connects resources | Device transport/session API planned | Device transport/session API planned |
| Storage | `NdsRaStorageRead`, `NdsRaStorageWrite` | Device `StorageRead` / `StorageWrite` | Device `StorageRead` / `StorageWrite` |

The current executable is a host control and validation path. For AIV and
AICPU it constructs the storage command on the host, then launches the device
work-request implementation to post that command. This proves the device Send
path, but it is not yet the planned device `StorageClient` API.

## Device Verbs And Connections

`src/client/include/nds/` owns the device-safe QP, WR, CQ, and
connection ABIs. The QP contains only addresses and queue metadata needed by
device code. The verbs layer accepts a QP and WR/CQ request. The connection layer
accepts a connection and transfer, then constructs the verbs WR for Send, Recv,
Read, or Write.

`resource/storage.cc` owns storage request state and selects the RA, AIV, or
AICPU backend for a storage command. It is not the implementation of the
AIV/AICPU verbs or connection APIs.

RMA is the umbrella used by HCOMM for RoCE and UB connections. The protocols
have different native resources: RoCE uses QPs and CQs, while UB uses Jetties
and JFCs. NDS currently implements only the RoCE binding through CANN RA and
must not present its QP/CQ types as a future UB API.

RA submits through `RaTypicalSendWr` and `rtRDMADBSend`. AIV and AICPU
provide device Send, Recv, RDMA Read, RDMA Write, SCQ poll, and RCQ poll paths.
The host control path can launch those operations for validation, while another
device operator can invoke the lower device APIs directly.

The host CPU owns the shared control path for every execution mode: CANN/RA
lifecycle, QP creation and connection, MR registration, TCP negotiation, and
resource lifetime. Device APIs must receive only NDS-owned device-safe views,
never host C++ objects or HCCP MR/QP handles.

The CPU server's verbs implementation owns its `libibverbs` context, PD, CQ,
RC QP, MR registration, Receive posting, RDMA Read/Write posting, and CQ
polling. It does not encode storage commands or make namespace decisions.

## Transport

The application owns `Runtime`, `Transport`, and `StorageClient` in that order.
`Runtime` owns AscendCL, the CANN runtime/network-service lifecycle, device
allocation and copy operations, and its `Memory` service. `Transport` borrows
the runtime and owns an `Endpoint`, one `QueuePair`, peer metadata, the TCP
bootstrap channel, and required AI-QP WR-ID storage. `Endpoint` owns the RA
loader/lifecycle and rdev; QPs and MRs own their handles and must be destroyed
before it. `StorageClient` borrows the runtime and transport, owns protocol
buffers, and registers them through the endpoint.

`Transport` performs control-path orchestration only. RDMA Send, Read, and
Write belong to execution backends operating on temporary non-owning resource
views. Protocol records and namespace decisions belong to `StorageClient`.

One QP per transport is an initial constraint. A future transport reserves a
control QP and selects data QPs round-robin without changing the storage API.
The device-safe `nds_device_transport` currently contains the control QP; it is
the ABI boundary that will grow a data-QP view when multi-QP support is added.

## Storage Client

`StorageClient` is the public NDS storage layer. It owns the command and
completion buffers, namespace capacity, request sequencing, record encoding,
command Send, and terminal completion validation. It exposes storage Read and
storage Write without exposing QPs or work requests.

The host implementation provides the current one-command baseline. Planned AIV
and AICPU implementations will expose the same storage semantics in their
device environments and use a device-safe transport/QP view produced by the
host control path.

## Application

Applications own process configuration, data buffers, request selection, and
workload verification. They call `StorageClient::read` or
`StorageClient::write` and do not manipulate QPs, work requests, CQs, or wire
codecs.

## Shared Boundaries

`src/common/connection.*` contains shared QP identity (`nds_qp_info`), TCP
bootstrap, and MTU policy. `src/common/protocol.*` contains the C-compatible
storage record ABI and codecs. Neither side exchanges HCCP handles, verbs
objects, AI-QP descriptors, queue addresses, or doorbell addresses.

The corresponding headers live in `src/include/nds/`. They are shared
between NDS targets, not an installed public SDK: `connection.h` is the C ABI
for QP identity and MTU policy, `connection.hh` owns the C++ TCP bootstrap
object, and `protocol.h` is the C ABI for storage records.
