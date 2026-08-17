# NPU Execution Modes

NDS has three execution modes: Host RA, AIV, and AICPU. They share the host
HCCP rdev/QP and MR control path described in
[HCCP QP and MR lifecycle](hccp-resources.md). Execution mode identifies where
a work request runs; it is independent from the work-request, transport, and
storage API layers.

For a storage Write, the CPU server RDMA Reads application data from the
NPU-advertised buffer into its namespace. For a storage Read, the CPU server
RDMA Writes namespace data into that buffer. AIV and AICPU post the command
Send; they do not issue the RDMA Read or RDMA Write that moves application
data.

The current capability matrix is:

| Layer | Host RA | AIV | AICPU |
|---|---|---|---|
| RMA | Send, RDMA Read/Write, send-CQ poll | Send/Receive, RDMA Read/Write, SCQ/RCQ polling | Send/Receive, RDMA Read/Write, SCQ/RCQ polling |
| Transport | Host `Transport` | Planned device API | Planned device API |
| Storage | Host `StorageClient` | `StorageRead` / `StorageWrite` | `StorageRead` / `StorageWrite` |

The current host executable can run an NDS storage Read or Write with any of
the three RMA execution modes. For AIV and AICPU, the host `StorageClient`
still constructs the command and observes the completion in the validation
executable; the device also exposes callable storage Read/Write operators for
other NPU operators.

## Host CPU Scope

All modes use the NPU-attached host CPU for client lifecycle, QP/MR setup, and
the current operator-launch and completion-observation flow. That is control
and orchestration work. The question that distinguishes the modes is where a
work request executes:

- `host-ra`: the host CPU executes the RA Send and runtime doorbell calls.
- `aiv`: an AIV operator executing on the NPU writes the WQE and rings the SQ
  doorbell.
- `aicpu`: a standard-CP1 operator executing in the NPU environment calls the
  device-side provider to post the Send.

Thus an AIV or AICPU launch originates on the host, but the host CPU does not
execute its RDMA post. The host launchers are adapters for the current
validation executable, not the eventual AIV/AICPU storage API.

| Execution mode | RDMA-post execution site | Host role for a request | QP mode | Local completion | Protocol completion |
|---|---|---|---|---|---|
| [`host-ra`](#host-ra) | Host CPU: `RaTypicalSendWr`, then `rtRDMADBSend` | Executes the post | OPBASE | Host RA CQ is available | CPU writes the NDS completion record; host copies and polls it |
| [`aiv`](#aiv) | NPU AIV: direct SQ/RQ/CQ and doorbell access | Creates and launches the device request | Configurable; OPBASE_EXT by default | Caller-owned SCQ/RCQ | CPU writes the NDS completion record; host copies and polls it |
| [`aicpu`](#aicpu) | NPU CP1: provider symbols first, address fallback | Creates and launches the device request | Configurable; NORMAL by default | Caller-owned SCQ/RCQ | CPU writes the NDS completion record; host copies and polls it |

The CPU polls its one `libibverbs` CQ for the command Receive and its signaled
terminal completion Write. HCCP AI-QP CQ handling, an AIV/AICPU launch, and a
host-RA local CQE are not NDS storage completion. The CPU-written NDS
completion record is authoritative.

Use `host-ra` first because it has the smallest hardware-specific surface.
Use `aiv` for direct vector-core WQE/doorbell posting and `aicpu` for a
provider-owned post from standard CP1. These are implementation choices, not
performance rankings. The host validation executable supports storage Read and
Write with all three work-request modes. Recorded hardware validation covers
one bounded storage Write in all three modes, a Host RA Read from a fresh
zeroed namespace, device Send/Receive with SCQ/RCQ polling in AIV and AICPU,
and direct device RDMA Read/Write with SCQ polling in both modes. It does not
yet cover an AIV or AICPU storage Read. NDS has not published throughput or
latency results.

The initial protocol permits one command in flight on one RC QP. Queueing,
multi-QP sessions, and an NPU Receive-based completion option are tracked in
[the roadmap](roadmap.md).

## Device Data Plane

AIV and AICPU operate on an NDS-owned device QP and connection without
receiving an HCCP handle or host C++ object. The host creates and connects the
QP, registers memory, and packages device-visible addresses into the shared
ABI below. This is the reusable device data plane that both execution modes
compile.

```text
Storage API
    -> device transport/session protocol (future)
        -> device connection: RDMA Send, Recv, Read, Write
            -> device verbs: post_send, post_recv, poll_cq
                -> provider symbols or SQ/RQ/CQ and doorbells
```

### Shared ABI

The C-compatible shared ABI lives in `src/client/include/nds/` and is split by
responsibility:

- `device_qp.h`: `nds_device_qp`, SQ/RQ/SCQ/RCQ descriptors, provider QP/CQ
  addresses, doorbell modes, and WR-ID sidecars.
- `device_verbs.h`: SGE, send WR, receive WR, CQ-poll request, completion, and
  operation result types.
- `device_connection.h`: a versioned `nds_device_connection` containing one QP
  and the transfer description used by connection operations.
- `device_operations.h`: the narrow host-launch dispatch record. It is an
  adapter into the connection layer, not the device API itself.

The QP and connection records remain local to the NPU endpoint and are never
exchanged with the CPU.

### Verbs APIs

AIV exposes the following AICore-callable functions in
`src/client/execution/aiv/device/aiv_device_api.h`:

```text
NdsAivPostSend(qp, send_wr, scratch, result)
NdsAivPostRecv(qp, recv_wr, scratch, result)
NdsAivPollCq(qp, poll_request, scratch, result)
```

Their implementation is in `src/client/execution/aiv/device/qp.cc` and
`src/client/execution/aiv/device/connection.cc`. Storage Read/Write is in
`src/client/execution/aiv/device/storage.cc`. Another AIV operator includes
the API header and compiles that implementation into its AICore translation
unit. CANN 9.0.0 rejects the otherwise valid multi-object AIV image at ACL load
time, so NDS deliberately builds the loadable entry and dataplane as one
translation unit. Standalone `nds_aiv_qp.o`, `nds_aiv_connection.o`, and
`nds_aiv_storage.o` are still emitted for compile and symbol verification, but
are not presented as a separately loadable kernel.

AICPU exports these symbols from `libnds_aicpu_standard.so`:

```text
NdsAicpuPostSend(qp, send_wr, result)
NdsAicpuPostRecv(qp, recv_wr, result)
NdsAicpuPollCq(qp, poll_request, result)
```

`PostSend` resolves the installed HNS provider's `ibv_exp_post_send` inside
standard CP1. `PostRecv` and `PollCq` try provider/verbs symbols first and use
the NDS queue-address implementation when those inline verbs operations are
not exported.

### Connection APIs

The connection layer accepts `nds_device_connection` plus an
`nds_device_transfer` and constructs the appropriate verbs WR. Both execution
environments expose:

```text
RdmaSend(connection, transfer, result)
RdmaRecv(connection, transfer, result)
RdmaRead(connection, transfer, result)
RdmaWrite(connection, transfer, result)
```

The concrete symbols are prefixed `NdsAiv` or `NdsAicpu`. Send, Read, and
Write lower to `PostSend` with distinct logical WR opcodes. Recv lowers to
`PostRecv`. CQ polling intentionally remains a verbs operation because callers
must choose SCQ or RCQ and consume explicit work completions.

The loaded entry points `NdsAivConnectionOp` and `NdsAicpuConnectionOp` only
validate and dispatch the versioned host-launch record into these connection
or qp functions. Other device operators do not need to call the entry points.

### Storage APIs

AIV and AICPU also implement the storage layer. They encode the command
record, Send it, and wait for the CPU-written completion record in device
memory:

```text
NdsAivStorageRead / NdsAivStorageWrite
NdsAicpuStorageRead / NdsAicpuStorageWrite
```

RNIC work completions are not NDS storage completion. Storage completion
remains the terminal protocol record written by the CPU endpoint.

## Host RA

Host RA is the NPU-host-CPU baseline. The host CPU itself executes the command
post through the CANN RA and runtime boundary; it does not launch a device
operator for that action.

```text
NPU host: RaTypicalSendWr(SEND) -> rtRDMADBSend
CPU:      Receive command -> RDMA Read or Write application data
CPU:      RDMA Write terminal completion record -> NPU completion memory
NPU host: aclrtMemcpy(device-to-host) polls terminal completion record
```

The host creates an OPBASE RC QP with `RaTypicalQpCreate`, connects it with
`RaTypicalQpModify`, and registers the NPU application, command, and
completion allocations with `RaRegisterMr`. `RaTypicalSendWr` returns
doorbell metadata, so `rtRDMADBSend` is required to submit the Send. A local
CQE is not protocol completion.

Key implementation paths:

- `src/client/execution/storage.cc`: host StorageClient command and completion flow.
- `src/client/execution/launch.cc`: host-example adapter into Host RA or a device operator.
- `src/client/execution/host_ra/qp.cc`: RA post and runtime doorbell.
- `src/client/resource/npu_ra_qp.cc`: QP and MR calls.
- `src/client/resource/npu_ra_context.cc`: runtime lifecycle,
  doorbell, and device-to-host completion copy.
- `src/server/protocol.cc` and `src/server/backend.cc`: CPU sequencing and
  CQ polling.

Start the CPU server:

```sh
build/nds_server --device <cpu-rdma-device> --gid-index <gid-index> \
  --listen <cpu-roce-ip> --tcp-port <port> --namespace-bytes 1048576
```

```sh
build/nds_client --execution host-ra \
  --ascendcl <cann-root>/aarch64-linux/lib64/libascendcl.so \
  --runtime <cann-root>/aarch64-linux/lib64/libruntime.so \
  --ra <cann-root>/aarch64-linux/lib64/libra.so \
  --npu-ip <npu-roce-ip> --logical-device 0 --physical-device 0 \
  --cpu-ip <cpu-roce-ip> --tcp-port <port> \
  --operation write --offset 0 --bytes 4096
```

Use `--operation read` to have the CPU RDMA Write a namespace range into the
NPU application buffer. Hardware invocations need a whole-process timeout.

The lifecycle and doorbell flow were derived from matching open-source HCOMM
source. NDS uses the ABI behavior but does not link HCOMM or copy its
implementation.

## AIV

The AIV execution mode currently implements the work-request layer through an
Ascend vector-core operator. The host creates and connects the AI QP, registers
NPU memory, and launches the operator, but the operator executes the
WQE/doorbell post.

```text
Host: RaAiQpCreate(OPBASE_EXT) -> package NDS device QP and connection
AIV:  connection API -> verbs API -> HNS WQE/doorbell or CQ consumption
CPU:  Receive command -> RDMA Read/Write data -> RDMA Write completion record
Host: poll copied NPU completion record
```

The device verbs, connection, and storage APIs for AIV are the ones described
in [Device Data Plane](#device-data-plane): `NdsAivPostSend`, `NdsAivPostRecv`,
`NdsAivPollCq` form the verbs layer, and `NdsAivRdmaSend`, `NdsAivRdmaRecv`,
`NdsAivRdmaRead`, and `NdsAivRdmaWrite` form the connection layer above them.
The loaded `NdsAivConnectionOp` kernel is only a host-launch adapter.

- Shared ABI: `src/client/include/nds/`.
- Device API: `src/client/execution/aiv/device/aiv_device_api.h`.
- qp/connection/storage: `src/client/execution/aiv/device/{qp,connection,storage}.cc`.
- Entry kernel: `src/client/execution/aiv/device/kernel/nds_aiv_kernel.cc`.
- Host launcher: `src/client/execution/aiv/host/launcher.cc`.

```sh
cmake -S . -B build-aiv -DNDS_CANN_ROOT=<cann-root> -DNDS_BUILD_AIV_KERNEL=ON
cmake --build build-aiv --parallel
```

Run the normal client arguments with `--execution aiv` and
`--aiv-kernel <absolute-path>/aiv/nds_aiv_kernel.o`. Select either
`--operation write` or `--operation read`; both use the AIV command-Send path,
and the CPU server selects the corresponding data-transfer direction.

SQ layout, cache maintenance, and doorbell encoding were learned from
open-source HCOMM AIV code and its patched HNS rdma-core provider source. NDS
ports one WQE form only, not HCOMM flags, collective state, or dispatchers.
The HNS hardware SQE opcode mapping is Send `0x0`, RDMA Write `0x3`, and RDMA
Read `0x5`; it is not the ibverbs work-request enum numbering.
The layout is CANN/provider specific and must be revalidated when that ABI
changes.

## AICPU

The AICPU execution mode currently implements the work-request layer through
an NDS-owned standard-CP1 operator and the CANN-matched HNS provider. The host
launches the operator but does not execute the provider post.

```text
Host: RaAiQpCreate(NORMAL) -> package NDS device QP and connection
CP1:  connection API -> exported verbs -> provider or direct RQ/CQ fallback
CPU:  Receive command -> RDMA Read/Write data -> RDMA Write completion record
Host: poll copied NPU completion record
```

The device verbs, connection, and storage APIs for AICPU are the ones described
in [Device Data Plane](#device-data-plane): the three exported verbs functions
(`NdsAicpuPostSend`, `NdsAicpuPostRecv`, `NdsAicpuPollCq`) and the four
connection functions (`NdsAicpuRdmaSend`, `NdsAicpuRdmaRecv`, `NdsAicpuRdmaRead`,
`NdsAicpuRdmaWrite`).

The host process does not load `libhns-rdmav25.so`; standard CP1 resolves it
in the NPU execution environment. The default NORMAL AI QP lets the provider
ring its normal-QP doorbell. Provider-OP mode may instead return doorbell
metadata, which the operator writes to the descriptor's SQ doorbell address.
Standard `ibv_post_recv` and `ibv_poll_cq` are commonly inline rather than
exported symbols, so the operator falls back to the descriptor's RQ and CQ
addresses when lookup fails.

- Shared ABI: `src/client/include/nds/`.
- Device API: `src/client/execution/aicpu/device/nds_aicpu_device_api.h`.
- qp/connection/storage: `src/client/execution/aicpu/device/{qp,connection,storage}.cc`.
- Entry point: `NdsAicpuConnectionOp` (a connection-dispatch adapter).
- Package: `src/client/execution/aicpu/device/package/nds_aicpu_standard.json.in`.

```sh
cmake -S . -B build-aicpu -DNDS_CANN_ROOT=<cann-root> -DNDS_BUILD_AICPU_KERNEL=ON
cmake --build build-aicpu --parallel
```

Install the generated package through CANN's supported customer-AICPU process,
then use `--execution aicpu` and
`--aicpu-kernel-config <path>/nds_aicpu_standard.json`. Select either
`--operation write` or `--operation read`; both use the AICPU command-Send
path, and the CPU server selects the corresponding data-transfer direction.

The AICPU shared object exports the three verbs functions and four connection
functions for use by other standard-CP1 operators. The device provider-loader pattern, AI-QP creation, and provider ABI were
learned from open-source HCOMM. NDS ports one provider post and does not wrap
HCOMM's transport kernel, reciprocal flags, synchronization, communicator,
batching, or collective logic. CP1 launch completion and RNIC CQEs are not NDS
storage command completion.
