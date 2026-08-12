# NDS agent notes

## Purpose

NDS is a small, distributable integration project for exercising a RoCE path between a Huawei Ascend NPU RNIC and a CPU-side RoCE RNIC. It will use the HCCP runtime installed by CANN, while owning its own implementation, build system, and documentation.

## Delivery goal

Build an application/library that can:

1. discover and dynamically load the installed HCCP shared library at runtime;
2. resolve and validate the HCCP ABI symbols needed for initialization, connection lifecycle, and memory registration;
3. establish a RoCE connection between an NPU RNIC and a host RNIC;
4. register and deregister application memory through the HCCP interface;
5. exchange the necessary peer/connection metadata through a transport that we implement; and
6. demonstrate a verifiable data-path operation with robust cleanup and useful diagnostics.

The implementation must be portable across compatible CANN installations: do **not** statically link against, vendor, or copy private HCCP implementation code.

## Implementation principles

- Use the public [HCCL](https://gitcode.com/cann/hccl) and [HCOMM](https://gitcode.com/cann/hcomm) open-source projects as behavioral and ABI-reference material. HCCP is part of the HCOMM reference tree; record the source basis for lifecycle and ABI decisions.
- Link only platform dependencies and the CPU-only `libibverbs` target. The portable NPU client dynamically loads the selected CANN root; see `docs/linkage.md` for the per-library ownership policy.
- Use `dlopen(3)`, `dlsym(3)`, and `dlerror(3)` (or the platform equivalent) for the CANN/HCCP runtime boundary.
- Keep all library names, search paths, symbol names, ABI/version validation, and optional-symbol handling behind a narrow runtime-loader module.
- Declare only the ABI-facing types/function-pointer signatures actually required by the project, and document the CANN/HCCP versions against which they were validated.
- Fail closed: if a required library, symbol, or version is missing or incompatible, emit a precise diagnostic and do not start the data path.
- Separate control plane (peer discovery and metadata exchange) from data plane (HCCP/RoCE operations).
- Make lifecycle ownership explicit: initialization, connection creation, memory registration, operation, deregistration, connection teardown, and library unload.
- The normal interoperability path is **one NPU and one CPU RNIC**. It must not initialize HCOMM/HCCL, consume a rank table, or use a second NPU.
- The direct NPU lifecycle is `aclInit` → `aclrtSetDevice` → `rtOpenNetService(--hdcType=18)` → `RaInit` → RA rdev/QP work → `RaDeinit` → runtime close → `aclFinalize`.
- HCOMM and TSD loaders remain diagnostic/reference tools only. Do not introduce them into the direct CPU-peer path without a separately validated ownership model.
- Never put host-specific addresses, credentials, private keys, SSH configuration, or operational change records in tracked files.

## Proposed work sequence

1. **Inventory runtime ABI** — locate installed HCCP shared objects and public/exported symbols; capture library and CANN version evidence.
2. **Map reference behavior** — inspect the open-source HCCP source only to understand initialization order, mandatory parameters, opaque handles, connection metadata, and teardown order.
3. **Define our adapter** — write a minimal independent HCCP dynamic-loader header/module with strongly typed function pointers and capability checks.
4. **Implement control plane** — select and implement a simple peer metadata exchange mechanism independent of HCCP internals.
5. **Implement connection and memory lifecycle** — create a connection, register test buffers, execute the minimal supported operation, then cleanly release all resources.
6. **Validate incrementally** — first loader/symbol checks, then NPU-local setup, then peer connection, memory registration, transfer, bidirectional validation, and negative/error cases.
7. **Package** — provide a documented build, runtime library-discovery configuration, example invocation, and compatibility matrix.

## Public software baseline

- Accelerator type: Huawei Ascend 910B3.
- Driver package version: `25.5.1`.
- CANN Toolkit version: `9.0.0`.
- The matching HCOMM source checkout is available in the sibling directory `../hcomm`, at tag `v9.0.0`.
- HCCP reference source is located in `../hcomm/src/platform/hccp`.

## Useful diagnostic commands

```sh
# NPU status, utilization, and processes
sudo npu-smi info

# HCCS link status for a card/chip
sudo npu-smi info -t hccs -i <card-id> -c <chip-id>

# Read an NPU RNIC address
sudo hccn_tool -i <npu-id> -ip -g

# Test reachability from an NPU RNIC
sudo hccn_tool -i <npu-id> -ping -g address <destination-ip> pkt 64

# Inspect host-side RDMA devices and their netdev mappings
rdma link show

# Inspect ELF dynamic symbols without executing the library
readelf -Ws <library.so>
nm -D --defined-only <library.so>
```

## Local-only information

- Put environment-specific addresses, host inventory, interface mappings, credentials, library locations discovered on a particular machine, and operational change records under `.local/`.
- `.local/` is ignored by Git and must never be committed.
- Do not place passwords, private keys, or SSH configuration in tracked files.

## Networking safety

Before changing a host network interface, verify that the active SSH management path uses a different interface. Persist intended changes through the host's network configuration system, then validate connectivity in both directions.

## Target interoperability model

The endpoint roles are intentionally asymmetric:

- **NPU endpoint:** exactly one selected NPU uses dynamically loaded HCCP/RA APIs as the RoCE **client**.
- **CPU endpoint:** the host-side Mellanox RNIC uses ordinary `libibverbs` as the RoCE **server**.
- **Transport:** both endpoints communicate directly over RoCE; HCCP is not required on the CPU endpoint.

Treat the interoperation boundary as an RDMA wire-compatibility exercise. Our control plane must exchange exactly the connection and memory metadata required by the selected QP mode, while the NPU-facing code obtains/consumes its side through HCCP and the CPU-facing code uses `libibverbs`.

## Validated QP-only milestone

The direct one-NPU/one-CPU path has been validated through QP establishment only: the CPU QP reaches `RTS` and the NPU-side `RaTypicalQpModify` succeeds after exchange of QPN, PSN, GID, GID index, and transport parameters. This milestone registers no memory and posts no work request. `NETWORK_OFFLINE` rdev setup uses the source-verified HCCP `NOTIFY` value (`1`), not `NO_USE` (`0`).

## AICPU generic RDMA-post work (developer reference)

### Scope

The AICPU route is an NDS-owned, minimal submission primitive for exactly one
signaled WQE.  It accepts RDMA Write, RDMA Read, and Send, but intentionally
does **not** import HCOMM/HCCL execution semantics: no rank/communicator state,
flag or acknowledgement buffers, batching/splitting, retries, dispatcher/event
poller, or peer synchronization.  The CPU endpoint remains ordinary
`libibverbs`.

The current CPU server exercises only RDMA Write.  The generic request ABI is
also prepared for Read and Send; their required CPU-side receive/remote-memory
setup is not yet an end-to-end feature.

### ABI and package

- Public request declaration: `include/nds/aicpu_roce_abi.h`.
- Device-kernel copy: `aicpu/include/nds_aicpu_roce_abi.h`.
- ABI version: `5`; `nds_aicpu_rdma_post_request_v2` is 80 bytes.
- The request carries opcode, AI-QP address, local/remote keys and addresses,
  length, and WR id. Reserved fields preserve the fixed package ABI.
- Kernel source/entry point: `aicpu/src/nds_aicpu_rdma_post.aicpu`,
  `NdsAicpuRdmaPost`.
- The custom-AICPU manifest must use CANN's `opInfo` schema and maps that entry
  point to the built NDS kernel shared object.

### Device-side provider post path

The host must **not** `dlopen` or link `libhns-rdmav25.so`: it is an NPU/AICPU
provider dependency.  The custom kernel resolves it at device execution time,
then resolves `ibv_exp_post_send`, constructs one SGE/WR, posts it, and
executes `dsb st`. No host-side library search is evidence that the provider
is or is not available inside the AICPU runtime.

The matching HCOMM AICPU normal-QP path ends after provider post and barrier.
It invokes dispatcher doorbell machinery only for non-normal QPs, so NDS does
not call `hrtRDMADBSend`, load `libaicpu_custom.so`, or pass a runtime stream.

### Reference basis and current evidence

Use the CANN 9.0.0 source checkouts on the target only:

- `~/src/hcomm/src/framework/device/utils/hccl_aicpu_utils.cc`
- `~/src/hcomm/src/platform/resource/transport/device/transport_device_ibverbs.cc`
- `~/src/hcomm/src/platform/common/dlhns_function.cc`
- `~/src/hcomm/src/platform/common/hccl_dl.cc`

The relevant normal-QP HCOMM chain is `HcclAicpuUtils::PostSend` →
`TransportDeviceIbverbs::HnsPostSend` → `HrtHnsIbvExpPostSend` → device
provider dynamic loading → provider post → barrier. Its separate non-normal
QP branch uses HCOMM's dispatcher. NDS ports only the normal-QP
single-WQE/provider-post/barrier path.

Target-only build and unit tests passed (7/7) after the ABI generalization;
the kernel export `NdsAicpuRdmaPost` was verified.  A single bounded NPU0
RDMA-Write attempt reached custom-kernel execution but failed while stream
synchronization reported `507018`, with CPU payload and guard bytes unchanged.
This does not validate the generic AICPU data path. An attempted device-memory
checkpoint itself faulted before it could report status, so it was removed:
AICPU must not directly dereference ordinary NPU device memory for diagnostics.
The subsequent v4 ACL-stream doorbell attempt produced the same error, which
falsified that route. Version 5 follows HCOMM's provider post convention and
does not add HCOMM KFC/SQE context, dispatcher, or synchronization flows.

`507018` is CANN's `ACL_ERROR_RT_AICPU_EXCEPTION`, not a timeout. The current
launcher therefore uses CANN's public AICPU stream configuration
`ACL_STREAM_FAST_LAUNCH | ACL_STREAM_FAST_SYNC`, resolved through the existing
ACL loader. This is isolated to the AICPU launcher; it does not alter the
host-RA/default-stream path.

An earlier `CPU_KERNEL_MODE=1` attempt used mismatched manifest and shared
object names and was rejected at load with `107000`. With the required paired
names `libnds_aicpu_roce.json` and `libnds_aicpu_roce.so`, the target-only
no-op probe succeeds. A provider-resolution probe also succeeds, as does a
request-read probe using the live 80-byte request, AI-QP address, MR keys, and
addresses. The target's `ibv_send_wr` size and field offsets match NDS's
128-byte transcription. One post attempt still returns AICPU task failure
(`507018`, device message `aicpu execute failed`) without CPU data visibility.
This confines the unresolved condition to `ibv_exp_post_send` execution or
the AI-QP/provider state it consumes, rather than package loading, device
provider resolution, ACL argument marshalling, or the hand-declared WR layout.

`NdsAicpuPostAttemptProbe` repeats the same one-WQE provider invocation while
deliberately suppressing its return value. It still raised `507018` on the
target, proving that the failure is an exception inside the provider call (or
its ABI/context), not a normal provider error return. The rdev now retains its
RA lite context (`disabled_lite_thread = false`), matching HCOMM's
`NetworkManager::InitRDMA` device-RoCE setup; one bounded Write with that
change still raised the same exception. This does not justify importing a
HCOMM dispatcher, completion poller, KFC/SQE context, or synchronization flow.

HCOMM AIV is not a portable custom-AICPU substitute. AIV runs as
`__aicore__`/`__gm__` and directly accesses queue and doorbell addresses under
its own device mapping contract. A target-only read-only custom-AICPU probe of
the `RaAiQpCreate` provider-QP address, and an earlier probe of the exported SQ
head/tail addresses, both raised `507018`. HCCP creates the `aiOpSupport=1`
objects through the HNS provider's private `libhns-rdma-hal.so` allocation
contract. CANN 9.0.0 publishes no custom-AICPU mapping/import API for that
memory. NDS therefore rejects a real AICPU RDMA post before launch on this
release; retain only package/provider/request probes until a supported mapping
contract is available.

### Validation discipline

Never build or test NDS on this Mac.  Synchronize the authoritative source
(excluding `.git`, `.local`, and build output) to `node200:~/src/nds`; build,
inspect CANN/HCOMM/HCCL sources, and run hardware validation only there.
Use one NPU (NPU0), `sudo -n`, a whole-process timeout, and no blind retries.
Keep target paths, logs, addresses, and operational details under `.local/`.
