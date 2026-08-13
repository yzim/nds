# AICPU RDMA Post

This guide covers NDS's standard-CP1 AICPU submission path.
Read [submission modes](modes.md) for the shared lifecycle and mode comparison.

## Basic function

AICPU submission launches an NDS-owned standard CP1 kernel that constructs one
signaled verbs WR and calls the CANN-matched HNS provider's
`ibv_exp_post_send`. The provider writes the WQE and rings the doorbell of an AI
NORMAL QP.

NDS creates this QP with `RaAiQpCreate(..., NORMAL)` and registers the NPU
device source allocation with `RaRegisterMr` before launching CP1. The request
contains the resulting source-MR local key and the CPU's advertised rkey. See
the shared [QP and MR lifecycle](hccp-resources.md) for the exact
resource exchange and teardown order.

Select the real data path with:

```text
--submission-mode aicpu
--aicpu-kernel-config <absolute-path-to-nds_aicpu_standard.json>
```

## Data path

```text
Host: RaAiQpCreate(NORMAL)
  -> connect QP and register NPU source MR
  -> aclrtBinaryLoadFromFile(standard manifest, CPU_KERNEL_MODE=0)
  -> launch NdsAicpuRdmaPost with one 80-byte v6 request

Standard CP1 kernel:
  dlopen(libhns-rdmav25.so)
  -> dlsym(ibv_exp_post_send)
  -> construct one ibv_sge + ibv_send_wr
  -> ibv_exp_post_send(ai_qp, wr)
       provider writes WQE and normal-QP sq.db_reg
  -> dsb st

RNIC -> CPU destination MR
Current test server checks payload and guards -> test-harness acknowledgment
```

The host application never links or loads `libhns-rdmav25.so`. Resolution
happens inside CP1 when the kernel executes.

## NDS interfaces

- Public request ABI:
  `src/npu_client/modes/aicpu/include/nds/aicpu_roce_abi.h`.
- Device-side ABI copy:
  `src/npu_client/modes/aicpu/device/include/nds_aicpu_roce_abi.h`.
- Provider ABI subset:
  `src/npu_client/modes/aicpu/device/include/nds_aicpu_hns_abi.h`.
- Kernel: `src/npu_client/modes/aicpu/device/nds_aicpu_rdma_post.aicpu`, entry point
  `NdsAicpuRdmaPost`.
- Host launcher: `src/npu_client/modes/aicpu/include/nds/aicpu_roce.hh` and
  `src/npu_client/modes/aicpu/launcher.cc`.
- Standard package manifest:
  `src/npu_client/modes/aicpu/device/package/nds_aicpu_standard.json.in`.
- Mode build ownership: `src/npu_client/CMakeLists.txt`.

ABI v6 is fixed at 80 bytes and carries opcode, logical device ID, AI-QP
address, local and remote keys and addresses, length, and WR ID. The kernel
accepts RDMA Write, Read, and Send. The current CPU server validates Write
only.

## Build, installation, and usage

Build only on the matching aarch64 CANN target:

```sh
cmake -S . -B build-aicpu \
  -DCMAKE_BUILD_TYPE=Release \
  -DNDS_BUILD_AICPU_KERNEL=ON \
  -DNDS_CANN_ROOT=<cann-root>
cmake --build build-aicpu --parallel
ctest --test-dir build-aicpu --output-on-failure
```

The standard artifacts are:

```text
build-aicpu/aicpu/libnds_aicpu_roce_standard.so
build-aicpu/aicpu/nds_aicpu_standard.json
build-aicpu/aicpu/aicpu_nds.tar.gz
```

The archive contains `aicpu_kernels_device/libnds_aicpu_roce_standard.so` and
its `bin_hash.cfg`. The SO is packaged with mode `750`.

Before mode-0 execution, install the package through CANN's supported customer
AICPU/operator installation procedure. On CANN 9.0.0, runtime discovery
requires both:

1. a vendor package beneath the CANN `opp/vendors` hierarchy; and
2. an additive package entry in the active `ascend_package_load.ini`.

Pointing `ASCEND_AICPU_PATH` at an unregistered package was not sufficient on
the validated release. NDS currently builds the installable artifacts but does
not modify a CANN installation or provide a production installer. Validation
used temporary private mount-namespace overlays to present the same registered
layout without changing installed files; that overlay is a test technique, not
the deployment procedure.

After installation, start `nds_verbs_server` as shown in the host guide and
run:

```sh
build-aicpu/nds_npu_qp_client \
  --ascendcl <cann-root>/aarch64-linux/lib64/libascendcl.so \
  --runtime <cann-root>/aarch64-linux/lib64/libruntime.so \
  --ra <cann-root>/aarch64-linux/lib64/libra.so \
  --npu-ip <npu-roce-ip> \
  --logical-device 0 \
  --physical-device 0 \
  --cpu-ip <cpu-roce-ip> \
  --tcp-port <control-port> \
  --submission-mode aicpu \
  --aicpu-kernel-config <absolute-build-or-installed-path>/nds_aicpu_standard.json \
  --execute
```

## HCOMM and HCCP reference basis

The minimal provider path and its resource requirements were derived from:

- `src/framework/device/hccl_aicpu_transport_interface.cc`:
  `RunTransportRoceTx`, HCOMM's standard-CP1 transport kernel.
- `src/framework/device/utils/hccl_aicpu_utils.cc`:
  `HcclAicpuUtils::PostSend` and its normal/non-normal QP branches.
- `src/platform/resource/transport/host/transport_direct_npu.cc`:
  standard mode-0 package loading, AI-QP export, and the
  `RunTransportRoceTx` launch path.
- `src/platform/resource/transport/device/transport_device_ibverbs.cc`:
  `TransportDeviceIbverbs::HnsPostSend`.
- `src/platform/common/dlhns_function.cc`: device-side provider loading.
- `src/platform/hccp/rdma_service/rs_rdma.c` and
  `rs_drv_rdma.c`: AI-QP creation and concrete mode selection.
- `third_party/rdma-core-42.7/providers/hns/hns_roce_u_ai.c` and
  `hns_roce_u_hw_v2.c`: provider post and doorbell behavior.

NDS ports only one-WR construction, provider invocation, and the store barrier.
It does not port `RunTransportRoceTx`'s three-flag peer protocol or HCOMM's KFC
communicator, dispatcher, batching, retries, rank state, or collective flows.

Earlier NDS revisions contained generic HCOMM bootstrap and RA probes that
dynamically loaded `libhcomm.so` and initialized a rank-table communicator.
Those probes did not submit this NDS primitive and were removed. Wrapping the
bundled `RunTransportRoceTx` kernel would also require its matching Rx-side
transport, flag buffers, and peer protocol; NDS does not implement that server
contract, so no such wrapper is retained. The HCOMM kernel remains useful only
as source evidence for CP1 packaging, AI-QP mode, and provider-call behavior.

## Choices and hidden decisions

### Why NORMAL QP is mandatory for this primitive

The decisive provider branch is mode-dependent:

```text
AI NORMAL (0): provider post writes WQE and calls hns_roce_update_sq_db
AI OP (2):     provider post writes WQE and returns db_info to its caller
```

HCCP rewrites an `OPBASE_EXT (4)` creation request to provider OP `(2)`. Our
first standard-CP1 post used that mode: the function returned, but CPU receive
PSN and payload did not change because no dispatcher submitted the returned
doorbell information. Changing the AICPU QP to NORMAL made the provider ring
`sq.db_reg`; the next bounded run advanced receive PSN by one and passed all
payload and guard checks.

This matches HCOMM's `TransportDirectNpu -> RunTransportRoceTx` scenario. For
the massive-connection BatchSendRecv fallback, HCOMM switches to
`TRANS_TYPE_DEVICE_DIRECT`, explicitly sets `machinePara.qpMode` to
`QPMode::NORMAL`, and then creates the AI QP used by that kernel.

HCOMM's broader AICPU/KFC transports may use OP/OPBASE_EXT because they also
invoke `AicpuDispatcher::RdmaSend` and integrate doorbells into SQE/task
ordering. That is a different execution model.

### Why standard CP1 mode is required

HCCP allocates AI-QP provider objects, shared queue memory, and RNIC doorbell
mappings for standard CP1. In mode 0 the NDS kernel runs in that process and
the normal-QP provider can access `sq.db_reg`.

An earlier custom AICPU experiment could load the provider and join HCCP's
shared-pool group, but it did not inherit CP1's RNIC doorbell mapping. Direct
mapping/import and doorbell-forwarding experiments failed with `507018`
(`ACL_ERROR_RT_AICPU_EXCEPTION`). CANN 9.0.0 exposes no supported API that
makes that model a replacement for CP1, so NDS no longer builds or exposes it.

### Package mechanism versus validation overlay

The standard AICPU package, `opInfo` manifest, hash file, executable mode, and
`CPU_KERNEL_MODE=0` launch are CANN mechanisms. The private mount namespace
used during validation only avoided persistent installation changes. A real
deployment should use CANN's supported customer-package installer and account
for upgrades and uninstall.

### Completion ownership

`aclrtSynchronizeStreamWithTimeout` means the CP1 function returned; it does
not mean the RNIC produced a CQE. HCCP owns this AI QP's CQ, so NDS does not
call `RaPollCq`. The client reports `SUBMITTED` and the current CPU server
returns `VERIFIED` or `FAILED` after its bounded payload/guard check. That
exchange is an integration-test harness, not a project-facing AICPU completion
interface. Defining the latter remains unfinished work.
