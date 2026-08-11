# NDS

NDS is an independent C++17/C project for a direct RoCE RC-QP path between one Huawei Ascend NPU RNIC and one host Mellanox RNIC.

- The NPU side dynamically loads the selected CANN installation's `libra.so` RA ABI.
- The CPU side is ordinary `libibverbs` and contains no CANN dependency.
- NDS owns its TCP control plane and wire format; it does not serialize private CANN structures.
- The normal path is intentionally **one NPU ↔ one CPU RNIC**. It does not use HCOMM, HCCL, a rank table, or a second NPU.

## Current validated milestone

The QP-only connection milestone is complete on the validated CANN release:

1. The NPU establishes its direct CANN/RA context.
2. It creates one RA RC QP and sends a project-owned endpoint record to the CPU.
3. The CPU `libibverbs` server transitions its QP `RESET → INIT → RTR → RTS`, returns its endpoint record, and keeps the QP alive.
4. The NPU passes the returned QPN/GID/PSN metadata to `RaTypicalQpModify`, which succeeds.
5. Both sides cleanly destroy their QPs.

This is deliberately **not** a data-path test: no memory region is registered and no send, receive, read, or write work request is posted.

## Architecture

```text
NPU client                                      CPU server
----------                                      ----------
AscendCL → CANN runtime → RA (`libra.so`)       libibverbs only
       │                                                │
create RA rdev and RC QP                         create RC QP
       │                                                │
       └──── project-owned TCP endpoint exchange ──────┘
                         │
              RaTypicalQpModify / RTR + RTS
```

### NPU lifecycle

The direct path is source-validated against the matching open-source HCOMM/HCCP tree but does not copy its implementation:

```text
aclInit
→ aclrtSetDevice
→ rtOpenNetService(--hdcType=18)
→ RaInit
→ RaRdevInit(NETWORK_OFFLINE, NOTIFY, ...)
→ RaTypicalQpCreate
→ endpoint exchange
→ RaTypicalQpModify
→ RaQpDestroy
→ RaRdevDeinit(..., NOTIFY)
→ RaDeinit
→ rtCloseNetService
→ aclFinalize
```

`NOTIFY` is the source-verified numeric value `1`. `NO_USE` (`0`) is invalid for the offline HDC rdev implementation.

## Project layout

```text
src/common/             Project-owned endpoint wire codec and TCP control plane
src/cpu_server/         Plain libibverbs CPU QP-only server
src/npu_client/
  npu_ra_context.cpp    Direct one-NPU CANN/RA lifecycle
  npu_ra_qp.cpp         RA rdev/QP creation and peer QP modification
  loaders/              Narrow dynamic ABI adapters
include/nds/            Public project headers
tests/                  Host-runnable protocol and RA-wrapper tests
```

## Build

A host build validates the project-owned code without CANN hardware:

```sh
cmake -S . -B build
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

The CPU server is built only where `libibverbs` development files are available. The NPU client receives absolute CANN library paths at runtime, keeping the private/version-coupled RA boundary dynamic.

## QP-only programs

The CPU server requires its RDMA device and GID index:

```sh
nds_verbs_server --device <rdma-device> --gid-index <index> \
  [--listen <cpu-ipv4>] [--tcp-port <port>] [--ib-port <port>]
```

The NPU client requires explicit CANN paths and one selected physical/logical NPU:

```sh
nds_npu_qp_client \
  --ascendcl <path-to-libascendcl.so> \
  --runtime <path-to-libruntime.so> \
  --ra <path-to-libra.so> \
  --npu-ip <npu-rnic-ipv4> \
  --logical-device <id> --physical-device <id> \
  --cpu-ip <cpu-rnic-ipv4> --execute
```

Use a whole-process timeout for accelerator experiments. Do not add a second NPU to this test. Sensitive deployment values and machine-specific commands belong under ignored `.local/`, never in this README.

## Data-path next step

Only after repeating the QP-only validation as needed should NDS add the next isolated stage: RA memory registration, explicit remote-memory metadata exchange, and one bounded operation using symbols verified in the installed `libra.so`. The CPU server must remain free of CANN dependencies.
