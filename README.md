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

An explicit, CANN-9.0.0-pinned `aicpu` submission mode is also available for NPU-to-CPU Tx. It creates an RA AI QP and launches CANN's installed `RunTransportRoceTx` AICPU kernel package; it is described below and remains hardware-validation pending.

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

For an offline HDC rdev, `NOTIFY` is `1`. `NO_USE` (`0`) does not work for this path. The host-submitted mode uses `RaRdevInitV2` with `disabledLiteThread=true`: NDS owns the send-CQ through `RaPollCq`, rather than racing HCOMM's legacy background Lite-CQ poller. The AICPU mode keeps that poller disabled too, but does **not** call `RaPollCq`: the sole completion owner is `aclrtSynchronizeStreamWithTimeout` on the dedicated AICPU launch stream.

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

`nds_npu_qp_client` defaults to `--submission-mode host-ra`, the bounded and hardware-verified RA/doorbell path. To exercise the second NPU submission approach, select `--submission-mode aicpu` and supply the **absolute** path to the selected CANN installation's `ccl_kernel.json`:

```sh
# CPU endpoint: remains ordinary libibverbs and exports one extra 24-byte sync MR.
nds_verbs_server --device <rdma-device> --gid-index <index> --aicpu-sync

# NPU endpoint: AI QP + CANN's installed RunTransportRoceTx AICPU kernel.
nds_npu_qp_client \
  --ascendcl <path-to-libascendcl.so> \
  --runtime <path-to-libruntime.so> \
  --ra <path-to-libra.so> \
  --npu-ip <npu-rnic-ipv4> --logical-device <id> --physical-device <id> \
  --cpu-ip <cpu-rnic-ipv4> --execute \
  --submission-mode aicpu \
  --aicpu-kernel-config <absolute-path-to-ccl_kernel.json>
```

This path transcribes HCOMM v9.0.0's public behavior without loading HCOMM: `RaAiQpCreate` creates an `OPBASE_EXT` AI QP, NDS exchanges the normal CPU destination MR plus a 24-byte CPU sync-MR descriptor, and a dedicated ACL stream launches one `RunTransportRoceTx` task. NDS allocates/registers the matching 24-byte NPU sync buffer. It waits through ACL stream synchronization rather than host `RaPollCq`; the CPU process still links only `libibverbs`.

The AICPU package parameter layout and kernel name are pinned to CANN 9.0.0. NDS dynamically loads the supplied installed package and never vendors its payload. This implementation is build- and unit-test-validated, but has **not yet completed a hardware payload/guard verification**; treat it as an explicit experimental mode and do not substitute it for the verified default path.

## Data-path interoperability note

The CPU selects `IBV_QP_PATH_MTU` from its **local active RDMA port**. NDS records the peer-reported MTU for diagnostics only: the HCCP v9.0.0 `TypicalQp` ABI contains no MTU field, and the matching HCOMM `RsDrvQpStateModifytoRtr` reference uses local `ibv_query_port(...).active_mtu`. Do not clamp the CPU QP MTU to the NPU control-plane record unless a separately validated NPU-side ABI makes that value authoritative.
