# NDS

NDS is a small experiment for bringing up a direct RoCE connection between one Ascend NPU RNIC and one Mellanox RNIC on the host.

The two sides intentionally stay simple:

- **NPU:** CANN's RA interface from `libra.so`.
- **CPU:** plain `libibverbs`.
- **Control plane:** a small TCP exchange owned by this project.

This is a one-NPU-to-one-CPU setup. It is not an HCCL job and does not need HCOMM, rank tables, or a second NPU in the normal path.

## What works today

The direct one-NPU/one-CPU data path has been verified on the target CANN release:

1. The NPU creates its ACL/runtime/RA context and one RC QP.
2. The CPU server creates one plain-`libibverbs` RC QP and moves it through `INIT`, `RTR`, and `RTS`.
3. The two programs exchange NDS-owned QP metadata over TCP, then the CPU sends a fixed versioned destination-MR descriptor.
4. In the default `host-ra` submission mode, the NPU calls `RaTypicalQpModify`, allocates device memory, registers the source MR through RA, posts one signaled 4096-byte RDMA Write, and submits the returned OPBASE runtime doorbell.
5. The NPU receives a successful send completion. The CPU verifies the deterministic payload and both 64-byte guard regions before both endpoints clean up.

An explicit `aicpu` submission mode is also available for NPU-to-CPU Tx. It creates an RA AI QP and loads an **NDS-built** AICPU package containing one one-way RDMA Write post. This mode remains hardware-validation pending: the tested CANN 9.0.0 host has no loadable `libhns-rdmav25.so` provider, so NDS now rejects the mode before kernel launch rather than issuing an opaque AICPU exception.

The normal data path uses exactly one NPU and one CPU RNIC. The CPU remains CANN-free; it does not load HCCP, HCOMM, HCCL, or TSD.

## How it fits together

```text
NPU client                                      CPU server
----------                                      ----------
AscendCL → CANN runtime → RA (`libra.so`)       libibverbs
       │                                                │
create RA rdev and RC QP                         create RC QP
       │                                                │
       └──── project-owned TCP endpoint exchange ──────┘
                         │
              RaTypicalQpModify / RTR + RTS
                         │
     destination-MR descriptor → one RDMA Write + OPBASE doorbell
                         │
             send CQE + CPU payload/guard validation
```

## Open-source references

Thanks to the contributors to [HCCL](https://gitcode.com/cann/hccl) and [HCOMM](https://gitcode.com/cann/hcomm). The lifecycle, transport, and ABI details used here are based on information already public in those projects. HCOMM includes the HCCP source that is useful when checking RA behavior.

At runtime, NDS uses the CANN libraries installed on the machine. Those installed libraries define the ABI that the NPU program actually calls.

For the QP path, the NPU side follows this order:

```text
aclInit
→ aclrtSetDevice
→ rtOpenNetService(--hdcType=18)
→ RaInit
→ RaRdevInitV2(NETWORK_OFFLINE, NOTIFY, disabledLiteThread=true, ...)
→ RaTypicalQpCreate
→ endpoint exchange
→ RaTypicalQpModify
→ RaRegisterMr → RaTypicalSendWr → rtRDMADBSend → RaPollCq
→ RaDeregisterMr → RaQpDestroy
→ RaRdevDeinit(..., NOTIFY)
→ RaDeinit
→ rtCloseNetService
→ aclFinalize
```

For an offline HDC rdev, `NOTIFY` is `1`. `NO_USE` (`0`) does not work for this path. The host-submitted mode uses `RaRdevInitV2` with `disabledLiteThread=true`: NDS owns the send-CQ through `RaPollCq`, rather than racing HCOMM's legacy background Lite-CQ poller. The AICPU mode keeps that poller disabled too, but does **not** call `RaPollCq`: ACL stream synchronization is the sole owner for AICPU execution completion. A successful stream synchronization means the kernel posted the WQE; the CPU still establishes data visibility by validating its destination MR after the control connection closes.

## Layout

```text
src/common/             TCP control plane and endpoint wire format
src/cpu_server/         CPU-side libibverbs server
src/npu_client/         NPU-side CANN/RA client
  loaders/              Dynamic CANN ABI loaders
include/nds/            Project headers
tests/                  Host-runnable protocol and wrapper tests
```

## Build

You can build and run the host-side tests without CANN hardware:

```sh
cmake -S . -B build
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

The CPU server is built only when `libibverbs` development files are available. The NPU client takes absolute library paths at runtime so it can use one selected CANN installation.

## Run the bounded data-path test

Start the CPU server with its RDMA device and GID index:

```sh
nds_verbs_server --device <rdma-device> --gid-index <index> \
  [--listen <cpu-ipv4>] [--tcp-port <port>] [--ib-port <port>] \
  [--bytes <1..65536>] [--post-close-hold-ms <0..60000>]
```

Then start the NPU client with one selected NPU and the matching CANN libraries:

```sh
nds_npu_qp_client \
  --ascendcl <path-to-libascendcl.so> \
  --runtime <path-to-libruntime.so> \
  --ra <path-to-libra.so> \
  --npu-ip <npu-rnic-ipv4> \
  --logical-device <id> --physical-device <id> \
  --cpu-ip <cpu-rnic-ipv4> --execute
```

Use a whole-process timeout when running accelerator experiments. The default server/client invocation runs one bounded data-path Write. Add `--qp-only` on both endpoints when validating only connection establishment; that mode registers no memory and posts no work request. Keep this test to one NPU and one CPU RNIC. Put machine-specific commands and deployment values in ignored `.local/`, not in this README.

## AICPU RoCE Tx submission mode

`nds_npu_qp_client` defaults to `--submission-mode host-ra`, the bounded and hardware-verified RA/doorbell path. The optional AICPU path is **owned and built by NDS**; it never loads the packaged HCOMM transport kernel or its reciprocal flag protocol.

Build the package on the aarch64 machine with the selected CANN installation:

```sh
cmake -S . -B build-aicpu \
  -DNDS_BUILD_AICPU_KERNEL=ON \
  -DNDS_CANN_ROOT=/usr/local/Ascend/cann-9.0.0
cmake --build build-aicpu --target nds_aicpu_kernel --parallel
```

This produces a normal aarch64 shared object, `build-aicpu/aicpu/libnds_aicpu_roce.so`, and its adjacent loader manifest, `build-aicpu/aicpu/libnds_aicpu_roce.json`. The manifest and `.so` must remain in that same directory when supplied to the launcher. Start the ordinary CPU verbs peer—there is no AICPU-specific CPU memory region or flag exchange:

```sh
nds_verbs_server --device <rdma-device> --gid-index <index>

nds_npu_qp_client \
  --ascendcl <path-to-libascendcl.so> \
  --runtime <path-to-libruntime.so> \
  --ra <path-to-libra.so> \
  --npu-ip <npu-rnic-ipv4> --logical-device <id> --physical-device <id> \
  --cpu-ip <cpu-rnic-ipv4> --execute \
  --submission-mode aicpu \
  --aicpu-kernel-config <absolute-path-to-libnds_aicpu_roce.json>
```

`NdsAicpuRdmaPost` has an NDS-owned, fixed 80-byte version-5 request ABI and posts exactly one signaled provider WQE for RDMA Write, RDMA Read, or Send. The current CLI exercises RDMA Write from the registered NPU buffer to the CPU's advertised destination MR. It performs no HCOMM/HCCL initialization, rank-table work, or wait for a CPU-written flag. The CPU remains a plain `libibverbs` endpoint and verifies its payload and guard bytes after TCP close.

CANN 9.0.0 supplies a supported AICPU compiler/package loader but does not expose a public AICPU RNIC-post API. The kernel therefore isolates the only required provider extension, `ibv_exp_post_send`, behind a minimal ABI declaration. The package must be built and run against the same CANN/provider release; a missing or incompatible provider fails the ACL launch rather than falling back to HCOMM. This is an implementation/build milestone and has **not yet completed hardware payload/guard verification**.

## Data-path interoperability note

The CPU selects `IBV_QP_PATH_MTU` from its **local active RDMA port**. NDS records the peer-reported MTU for diagnostics only: the HCCP v9.0.0 `TypicalQp` ABI contains no MTU field, and the matching HCOMM `RsDrvQpStateModifytoRtr` reference uses local `ibv_query_port(...).active_mtu`. Do not clamp the CPU QP MTU to the NPU control-plane record unless a separately validated NPU-side ABI makes that value authoritative.
