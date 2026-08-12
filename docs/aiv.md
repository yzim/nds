# AIV RDMA Write

This guide covers the NDS-owned vector-core path. Read
[submission modes](modes.md) before using it; AIV has a different QP mode and
completion model from host RA and AICPU.

## Basic function

AIV submission moves WQE construction and SQ doorbell access into an Ascend
vector-core kernel. The host still creates and connects the HCCP AI QP and
registers memory, but it does not submit the RDMA request.

NDS creates this QP with `RaAiQpCreate(..., OPBASE_EXT)`. HCCP returns the
AI send-WQ descriptor that the AIV execution environment can use directly;
NDS still obtains the source-MR local key through `RaRegisterMr`. See the
shared [QP and MR lifecycle](modes.md#qp-setup-through-hccp) for the full
connection and ownership sequence.

Select it with:

```text
--submission-mode aiv --aiv-kernel <absolute-path-to-nds_aiv_roce.o>
```

The current AIV operation is RDMA Write.

## Data path

```text
Host: RaAiQpCreate(OPBASE_EXT) -> receive AI SQ data-plane descriptor
Host: copy descriptor + one NDS request into ordinary NPU memory
Host: aclrtBinaryLoadFromFile(AIV object)
      -> aclrtBinaryGetFunction(NdsAivRdmaWrite)
      -> aclrtLaunchKernelWithHostArgs

AIV kernel:
  read SQ head/tail
  -> encode one HNS RC Write WQE in the SQ
  -> clean WQE cache lines
  -> write the packed hardware SQ doorbell
  -> publish the new SQ head

RNIC -> CPU destination MR
Current test server checks payload and guards -> test-harness acknowledgment
```

The kernel may issue up to 16 Writes in one launch, or the host may perform up
to 16 launches. The product of `--aiv-write-count` and
`--aiv-launch-count` is bounded to 16.

## NDS interfaces

- Public host/device ABI:
  `src/npu_client/modes/aiv/include/nds/aiv_roce_abi.h`.
- AIV kernel: `src/npu_client/modes/aiv/kernel/nds_aiv_roce.cpp`, entry point
  `NdsAivRdmaWrite`.
- Host launcher: `src/npu_client/modes/aiv/include/nds/aiv_roce.hpp` and
  `src/npu_client/modes/aiv/launcher.cpp`.
- Mode build ownership: `src/npu_client/modes/aiv/CMakeLists.txt` and its
  `kernel/CMakeLists.txt`.
- QP descriptor source: `RaAiQpCreate` through `NpuRaQp`.
- Runtime launch boundary: dynamically resolved AscendCL binary and kernel
  APIs.

The AIV request contains the copied send-WQ descriptor, service level, local
and remote keys and addresses, transfer length, and bounded Write count. It
does not receive an HCOMM communicator, rank table, dispatcher, flag buffers,
or collective task graph.

## Build and usage

Build only on an aarch64 CANN target with the matching CCEC toolchain:

```sh
cmake -S . -B build-aiv \
  -DCMAKE_BUILD_TYPE=Release \
  -DNDS_BUILD_AIV_KERNEL=ON \
  -DNDS_CANN_ROOT=<cann-root>
cmake --build build-aiv --parallel
ctest --test-dir build-aiv --output-on-failure
```

The generated kernel is:

```text
build-aiv/aiv/nds_aiv_roce.o
```

Start `nds_verbs_server` as shown in the host guide, then run:

```sh
build-aiv/nds_npu_qp_client \
  --ascendcl <cann-root>/aarch64-linux/lib64/libascendcl.so \
  --runtime <cann-root>/aarch64-linux/lib64/libruntime.so \
  --ra <cann-root>/aarch64-linux/lib64/libra.so \
  --npu-ip <npu-roce-ip> \
  --logical-device 0 \
  --physical-device 0 \
  --cpu-ip <cpu-roce-ip> \
  --tcp-port <control-port> \
  --submission-mode aiv \
  --aiv-kernel <absolute-build-path>/aiv/nds_aiv_roce.o \
  --aiv-write-count 1 \
  --aiv-launch-count 1 \
  --execute
```

For bounded repeated submission, set either count up to 16 while keeping their
product at or below 16.

## HCOMM reference basis

The queue layout and ordering were derived from HCOMM's AIV implementation and
its patched HNS provider sources:

- `src/algorithm/base/alg_aiv_template/aiv_communication_base.h`: direct SQ
  head/tail access, RC WQE construction, cache maintenance, doorbell write,
  and head publication.
- `third_party/rdma-core-42.7/providers/hns/hns_roce_u_ai.c`: AI-QP creation
  modes and exported data-plane descriptors.
- `third_party/rdma-core-42.7/providers/hns/hns_roce_u_hw_v2.c`: HNS v2 SQ
  doorbell encoding and write ordering.
- `src/platform/hccp/rdma_service/rs_rdma.c`: HCCP AI-QP creation and mode
  normalization.

NDS independently implements only the one-WQE RC Write subset required by its
wire test. It does not compile or invoke the HCOMM AIV algorithm.

## Choices and hidden decisions

### Why OPBASE_EXT is used

NDS asks HCCP for `OPBASE_EXT (4)`. HCCP normalizes that to provider OP mode
`(2)` and returns the SQ data-plane addresses needed by AIV. In provider OP
mode, `ibv_exp_post_send` would return doorbell metadata instead of ringing a
normal-QP doorbell, but AIV does not call that function: it owns the SQ and
doorbell writes directly.

This differs from AICPU. Sharing the same rdev/MR lifecycle does not imply that
the two submitters should use the same QP mode.

### Addressability is execution-environment specific

The queue and MMIO addresses returned for AI operation are usable by the AIV
`__aicore__`/`__gm__` execution model. Their presence in a host structure does
not make them ordinary host or custom-AICPU virtual addresses. Attempts to
dereference them from a separate custom AICPU process failed and are not a
supported mapping strategy.

### Completion ownership

HCCP owns the AI QP's CQ. `RaPollCq` is not valid for this handle and must not
be used. ACL stream synchronization proves that the AIV kernel finished
publishing requests, not that the RNIC completed them. The CPU payload/guard
acknowledgment is only the current integration-test harness; it keeps QP and
MR resources alive while the Write is checked. NDS does not yet expose a
project-facing AIV completion interface.

### Hardware coupling

AIV embeds HNS RC WQE and doorbell layout. Any change in provider/device ABI,
WQE size, owner-bit convention, cache operation, or doorbell format requires
source revalidation. This is why host RA remains the portability baseline.
