# NPU Backends

NDS has three NPU backends. They share the HCCP rdev/QP and MR lifecycle in
[HCCP QP and MR lifecycle](hccp-resources.md). A backend changes only how the
NPU posts its storage-command Send and manages its local work. CPU storage
execution and NDS protocol completion are the same in every mode.

| Backend | Command post | QP mode | Local completion | Protocol completion |
|---|---|---|---|---|
| [`host-ra`](#host-ra) | Host calls `RaTypicalSendWr`, then `rtRDMADBSend` | OPBASE | Host RA CQ is available | CPU writes the NDS completion record; host copies and polls it |
| [`aiv`](#aiv) | AIV writes a Send WQE and SQ doorbell | OPBASE_EXT/provider OP | HCCP internal today | CPU writes the NDS completion record; host copies and polls it |
| [`aicpu`](#aicpu) | Standard CP1 calls provider `ibv_exp_post_send` | NORMAL | HCCP internal today | CPU writes the NDS completion record; host copies and polls it |

The CPU polls its one `libibverbs` CQ for the command Receive and its signaled
terminal completion Write. HCCP AI-QP CQ handling, an AIV/AICPU launch, and a
host-RA local CQE are not NDS storage completion. The CPU-written NDS
completion record is authoritative.

Use `host-ra` first because it has the smallest hardware-specific surface.
Use `aiv` for direct vector-core WQE/doorbell posting and `aicpu` for a
provider-owned post from standard CP1. These are implementation choices, not
performance rankings. NDS has validated one bounded storage Write in all three
modes and a Host RA Read from a fresh zeroed namespace; it has not published
throughput or latency results.

The initial protocol permits one command in flight on one RC QP. Queueing,
multi-QP sessions, and an NPU Receive-based completion option are tracked in
[the roadmap](roadmap.md).

## Host RA

Host RA is the NPU-host-CPU baseline. It uses the CANN RA and runtime boundary
without a device kernel.

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

- `src/client/backend/host_ra/backend.cc`: RA post and runtime doorbell.
- `src/client/backend/support/core/npu_ra_qp.cc`: QP and MR calls.
- `src/client/backend/support/core/npu_ra_context.cc`: runtime lifecycle,
  doorbell, and device-to-host completion copy.
- `src/server/protocol.cc` and `src/server/backend.cc`: CPU sequencing and
  CQ polling.

Start the CPU server:

```sh
build/nds_server --device <cpu-rdma-device> --gid-index <gid-index> \
  --listen <cpu-roce-ip> --tcp-port <port> --namespace-bytes 1048576
```

```sh
build/nds_client --backend host-ra \
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

The AIV backend posts the storage-command Send from an Ascend vector-core
kernel. The host still creates and connects the AI QP and registers all NPU
memory.

```text
Host: RaAiQpCreate(OPBASE_EXT) -> copy AI SQ descriptor and post request
AIV:  write one HNS RC Send WQE -> clean cache -> ring SQ doorbell
CPU:  Receive command -> RDMA Read/Write data -> RDMA Write completion record
Host: poll copied NPU completion record
```

`NdsAivRdmaPost` owns only the direct WQE/doorbell action. Opcode `0` is Send
with zero remote address/key; the ABI retains opcode `3` for its narrow
one-sided Write primitive. The storage path posts one command.

- ABI: `src/client/backend/aiv/include/nds/aiv_roce_abi.h`.
- Kernel: `src/client/backend/aiv/kernel/nds_aiv_roce.cc`.
- Host launcher: `src/client/backend/aiv/launcher.cc`.

```sh
cmake -S . -B build-aiv -DNDS_CANN_ROOT=<cann-root> -DNDS_BUILD_AIV_KERNEL=ON
cmake --build build-aiv --parallel
```

Run the normal client arguments with `--backend aiv` and
`--aiv-kernel <absolute-path>/aiv/nds_aiv_roce.o`.

SQ layout, cache maintenance, and doorbell encoding were learned from
open-source HCOMM AIV code and its patched HNS rdma-core provider source. NDS
ports one WQE form only, not HCOMM flags, collective state, or dispatchers.
The layout is CANN/provider specific and must be revalidated when that ABI
changes.

## AICPU

The AICPU backend launches an NDS-owned standard CP1 kernel that posts one
storage-command Send through the CANN-matched HNS provider.

```text
Host: RaAiQpCreate(NORMAL) -> launch NdsAicpuRdmaPost
CP1:  dlopen(libhns-rdmav25.so) -> dlsym(ibv_exp_post_send) -> post Send
CPU:  Receive command -> RDMA Read/Write data -> RDMA Write completion record
Host: poll copied NPU completion record
```

The host process does not load `libhns-rdmav25.so`; standard CP1 resolves it
in the NPU execution environment. A NORMAL AI QP is required because the
provider rings its normal-QP doorbell. Provider-OP mode returns doorbell
metadata and needs a separate dispatcher to ring it.

- Request ABI: `src/client/backend/aicpu/include/nds/aicpu_roce_abi.h`.
- CP1 source: `src/client/backend/aicpu/device/nds_aicpu_rdma_post.aicpu`.
- Entry point: `NdsAicpuRdmaPost`.
- Package: `src/client/backend/aicpu/device/package/nds_aicpu_standard.json.in`.

```sh
cmake -S . -B build-aicpu -DNDS_CANN_ROOT=<cann-root> -DNDS_BUILD_AICPU_KERNEL=ON
cmake --build build-aicpu --parallel
```

Install the generated package through CANN's supported customer-AICPU process,
then use `--backend aicpu` and
`--aicpu-kernel-config <path>/nds_aicpu_standard.json`.

The device provider-loader pattern, AI-QP creation, and provider ABI were
learned from open-source HCOMM. NDS ports one provider post and does not wrap
HCOMM's transport kernel, reciprocal flags, synchronization, communicator,
batching, or collective logic. CP1 launch completion and internal AI-QP CQ
handling are not NDS command completion.
