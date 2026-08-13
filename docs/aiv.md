# AIV Backend

The AIV backend posts the NDS storage-command Send from an Ascend vector-core
kernel. The host still creates and connects the AI QP and registers NPU
application, command, and completion memory.

## Data Path

```text
Host: RaAiQpCreate(OPBASE_EXT) -> copy AI SQ descriptor and post request
AIV:  write one HNS RC Send WQE -> clean cache -> ring SQ doorbell
CPU:  Receive command -> RDMA Read/Write data -> RDMA Write completion record
Host: poll copied NPU completion record
```

`NdsAivRdmaPost` owns only the direct WQE/doorbell action. Opcode `0` is Send
and has zero remote address/key; the ABI also retains opcode `3` for its narrow
one-sided Write primitive. The storage path posts exactly one command.

## Interfaces and Usage

- ABI: `src/npu_client/modes/aiv/include/nds/aiv_roce_abi.h`.
- Kernel: `src/npu_client/modes/aiv/kernel/nds_aiv_roce.cc`, entry
  `NdsAivRdmaPost`.
- Host launcher: `src/npu_client/modes/aiv/launcher.cc`.

Build on the target only:

```sh
cmake -S . -B build-aiv -DNDS_CANN_ROOT=<cann-root> -DNDS_BUILD_AIV_KERNEL=ON
cmake --build build-aiv --parallel
```

Run the normal NPU client arguments with `--submission-mode aiv` and
`--aiv-kernel <absolute-path>/aiv/nds_aiv_roce.o`. The CPU server and command
options are described in [Host RA](host-ra.md).

## Reference Basis and Limits

The SQ layout, cache maintenance, and doorbell encoding were derived from
HCOMM's AIV communication code and its patched HNS rdma-core provider sources.
NDS ports only one WQE form; it does not include HCOMM flags, collectives,
dispatchers, or rank state. HCCP's AI-QP CQ handling proves neither CPU data
completion nor NDS command completion; the CPU-written record is authoritative.

The WQE and doorbell layout is CANN/provider specific and must be revalidated
when that ABI changes.
