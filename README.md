# NDS

NDS validates direct RoCE interoperability between one Ascend NPU RNIC and one
CPU-side RNIC. The NPU endpoint uses CANN RA APIs; the CPU endpoint uses plain
`libibverbs`. NDS owns the TCP control plane, wire format, build, and device
submission code.

This is not an HCCL job. The normal path uses one NPU and one CPU RNIC, does
not initialize HCOMM or HCCL, does not use a rank table, and does not require a
second NPU.

## Current status

The target CANN 9.0.0 environment has validated a bounded 4096-byte RDMA Write
for all three submission modes. The CPU verifies every payload byte and both
64-byte guard regions before it acknowledges the transfer.

| Mode | Where the request is submitted | NPU completion evidence | Guide |
|---|---|---|---|
| `host-ra` | NPU host process through RA and runtime doorbell APIs | `RaPollCq`, then CPU verification | [Host RA](docs/host-ra.md) |
| `aiv` | NDS AIV kernel writes the HNS SQ and doorbell | CPU verification; HCCP owns the AI CQ | [AIV](docs/aiv.md) |
| `aicpu` | NDS standard-CP1 kernel calls the HNS provider | CPU verification; HCCP owns the AI CQ | [AICPU](docs/aicpu.md) |

`host-ra` is the default and the first mode to use on a new target.

## Architecture

```text
NPU client                                      CPU server
----------                                      ----------
AscendCL -> CANN runtime -> RA (`libra.so`)     `libibverbs`
       |                                                |
create rdev, RC QP, and source MR               create RC QP and destination MR
       |                                                |
       +------ NDS TCP endpoint and MR exchange -------+
                              |
                     mode-specific RDMA Write
                              |
                  CPU payload and guard verification
```

The CPU is CANN-free. It never loads HCCP, HCOMM, HCCL, or TSD.

## HCCP resources

The NPU client creates one HCCP rdev and one RC QP. Host RA uses
`RaTypicalQpCreate`; AIV and AICPU use `RaAiQpCreate` with different QP modes
because their doorbell ownership differs. `RaGetQpAttr` supplies the QPN, PSN,
GID, and GID index that NDS exchanges with the CPU over TCP. The CPU uses that
record to configure its own verbs QP, and the NPU applies the CPU record with
`RaTypicalQpModify`.

For a Write, the CPU registers its destination buffer with `ibv_reg_mr` and
sends its address, length, and rkey. The NPU allocates device memory, registers
it through `RaRegisterMr`, and uses the returned lkey as the local SGE key. NDS
does not send QP handles, MR handles, AI-QP descriptors, or provider-private
addresses over TCP. It keeps both endpoints' QPs and MRs alive until the CPU
verifies the remote write. [Submission modes](docs/modes.md#qp-setup-through-hccp)
has the complete setup, key, and teardown sequence.

## Repository layout

```text
src/common/       TCP control plane, NDS wire format, and MTU policy
src/npu_client/   NPU RA lifecycle, runtime loaders, submission modes, and probes
src/cpu_server/   CPU `libibverbs` endpoint
tests/            Unit tests and a test-only runtime fixture
docs/             Mode guides, linkage policy, and integration decisions
```

NDS is C++20. ABI and wire headers remain C-compatible where they cross a
runtime or device boundary.

## Build

Build and test only on the matching aarch64 CANN target. Do not build or test
on a development Mac.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

To build the optional device artifacts, add both options and select the target
CANN installation:

```sh
cmake -S . -B build-device -DCMAKE_BUILD_TYPE=Release \
  -DNDS_BUILD_AIV_KERNEL=ON \
  -DNDS_BUILD_AICPU_KERNEL=ON \
  -DNDS_CANN_ROOT=<cann-root>
cmake --build build-device --parallel
ctest --test-dir build-device --output-on-failure
```

## Run the baseline

Start the CPU endpoint first:

```sh
build/nds_verbs_server \
  --device <cpu-rdma-device> \
  --gid-index <cpu-gid-index> \
  --listen <cpu-roce-ip> \
  --tcp-port <control-port> \
  --bytes 4096
```

Then start the NPU endpoint:

```sh
build/nds_npu_qp_client \
  --ascendcl <cann-root>/aarch64-linux/lib64/libascendcl.so \
  --runtime <cann-root>/aarch64-linux/lib64/libruntime.so \
  --ra <cann-root>/aarch64-linux/lib64/libra.so \
  --npu-ip <npu-roce-ip> \
  --logical-device 0 --physical-device 0 \
  --cpu-ip <cpu-roce-ip> --tcp-port <control-port> \
  --submission-mode host-ra --execute
```

Use a whole-process timeout for hardware runs. Add `--qp-only` to both
endpoints to validate connection setup without registering memory or posting a
work request. Keep addresses, target paths, logs, and operational commands in
ignored `.local/` files.

## Guides

- [Submission modes and common lifecycle](docs/modes.md)
- [Host RA submission](docs/host-ra.md)
- [AIV submission](docs/aiv.md)
- [AICPU submission](docs/aicpu.md)
- [Linkage and runtime ABI policy](docs/linkage.md)

The CPU QP selects path MTU from its local active port. The peer MTU carried in
the NDS endpoint record is diagnostic only because the CANN 9.0.0 `TypicalQp`
ABI does not provide an NPU-side path-MTU setting.
