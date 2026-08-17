# HCCP QP, MR, and Runtime ABI

This guide describes the common resource model used before any NDS execution
mode posts a request, and the runtime library boundary NDS builds against. It
covers the NPU HCCP/RA rdev, QP, and memory registration; the independent CPU
verbs resources; the NDS transport bootstrap; the per-library link/load
decision; and teardown. Mode-specific posting and CQ handling are described in
[NPU execution modes](npu-backends.md).

In the source tree, HCCP lifecycle code is the shared resource layer under
`src/client/resource`; RA posting remains under
`src/client/execution/ra`, while AIV and AICPU code lives under
`src/client/execution`. Connection control uses those resources without
exposing HCCP handles to `StorageClient`.

## Ownership model

The NPU process owns one HCCP rdev, one HCCP QP, and separate registered NPU
application, command, and completion allocations. The CPU process independently
owns its verbs context, PD, CQ, QP, command Receive record, memory namespace,
and completion-record source buffer. The TCP transport bootstrap never
transfers an HCCP or verbs object between processes.

NDS exchanges only versioned NDS records:

- Endpoint record: QPN, PSN, GID, GID index, port, QoS/retry values, and a
  diagnostic MTU.
- Storage bootstrap: NPU completion-record address, length, rkey, and access.
- Namespace record: CPU memory-backed namespace capacity.
- Command record: request ID, operation, namespace range, and NPU application
  memory descriptor.

It must not exchange HCCP QP or MR handles, AI-QP descriptors, queue or
doorbell addresses, or provider-private objects. Those addresses are valid only
in the execution environment that owns them.

## NPU rdev and QP

`NpuRaContext` initializes AscendCL, the runtime network service, and RA once:

```text
aclInit -> aclrtSetDevice -> rtOpenNetService(--hdcType=18) -> RaInit
```

`NpuRaQp` creates the rdev with `RaRdevInitV2` using the selected physical NPU
and NPU RNIC IPv4 address. NDS uses `NETWORK_OFFLINE`, `NOTIFY (1)`, and an
enabled Lite context (`disabled_lite_thread=false`). `NO_USE (0)` is not valid
for the offline rdev lifecycle used by this path.

NDS creates exactly one RC QP on that rdev. The selected mode determines the
HCCP creation entry point and requested QP mode:

| Mode | HCCP creation | Requested QP mode | Resource purpose |
|---|---|---|---|
| `ra` | `RaTypicalQpCreate` | OPBASE (`2`) | RA post returns runtime doorbell information. |
| `aiv` | `RaAiQpCreate` | Configurable; OPBASE_EXT (`4`) by default | HCCP returns caller-owned SQ/RQ/SCQ/RCQ dataplane data. |
| `aicpu` | `RaAiQpCreate` | Configurable; NORMAL (`0`) by default | CP1 probes provider symbols first and uses the same dataplane record for fallbacks. |

For AI QPs, NDS supplies an RC QP shape with one SGE per work request and
applies traffic class, service level, retry timeout, and retry count after
creation. With caller CQ polling enabled, HCCP returns provider addresses and
SQ/RQ/SCQ/RCQ dataplane information. NDS converts these into one versioned
descriptor containing queue geometry, producer/consumer state, and
record/MMIO doorbell addresses. It remains local to the NPU environment.

## Peer connection

After QP creation, `RaGetQpAttr` returns the NPU QPN, PSN, GID, and GID index.
NDS serializes those public peer fields into its endpoint record through
`src/common/transport.*`. The CPU server creates an independent RC QP using
`libibverbs`, moves it through `INIT`, `RTR`, and `RTS`, and returns its own
endpoint record.

The NPU converts the CPU endpoint record into the RA representation and calls
`RaTypicalQpModify`. The CPU uses its local active-port MTU for
`IBV_QP_PATH_MTU`. The NDS peer MTU is diagnostic only because the CANN 9.0.0
`TypicalQp` ABI has no NPU-side path-MTU field.

## Memory registration and keys

The CPU registers a command Receive record, memory-backed namespace, and
completion-record source buffer. The NPU separately registers application,
command, and completion allocations. The application MR grants CPU remote read
and remote write; the command MR supplies the NPU Send SGE key; and the
completion MR grants CPU remote write and is exchanged once during TCP
bootstrap. Commands carry the application address, rkey, length, and access
direction.

## Lifetime and teardown

Posting a command does not permit either endpoint to release its resources.
The QPs, MRs, queue WR-ID sidecars, and NPU allocations remain valid until the
NPU consumes its signaled send CQE, the CPU completes its data and terminal
completion Write, and the NPU observes that completion record.

NDS tears down in reverse ownership order:

```text
RaDeregisterMr -> free NPU allocation -> RaQpDestroy
-> RaRdevDeinit(NOTIFY) -> RaDeinit -> rtCloseNetService -> aclFinalize
```

The CPU deregisters its verbs MR and releases its QP, CQ, PD, and context as
the CPU server resources leave scope.

## Runtime libraries and ABI

NDS makes one explicit decision per library. Stable platform and CPU verbs APIs
are linked normally. Version-coupled CANN interfaces stay behind narrow runtime
loaders, except for the optional version-pinned public AscendCL build.

### Direct one-NPU / one-CPU path

The production interoperability path does **not** use HCOMM, HCCL, TSD, a rank table, or another NPU. It establishes one NPU RA QP against one CPU `libibverbs` QP.

| Library | NDS use | Build treatment | Rationale |
|---|---|---|---|
| POSIX / `libdl` / threads | sockets, TCP transport bootstrap, dynamic loading | Link | Stable system ABI. |
| `libibverbs.so` | CPU-side PD/CQ/QP operations | Link, CPU target only | The CPU endpoint remains CANN-free. |
| `libascendcl.so` | lifecycle plus AIV/AICPU binary, argument, kernel, and stream APIs when selected | Dynamic by default; optional version-pinned public-ACL link mode | Public lifecycle boundary; dynamic default preserves host build portability. |
| `libruntime.so` | `rtOpenNetService`, `rtCloseNetService`, `rtRDMADBSend` | Runtime-load | Required by the direct NPU RA lifecycle and default OPBASE Lite doorbell ring; not exposed through a stable NDS-facing SDK contract. |
| `libra.so` | `RaInit`, rdev/QP lifecycle, MR registration, send/CQ APIs | Runtime-load | Required ABI is private/version-coupled; NDS transcribes only the interfaces it uses, including optional `RaAiQpCreate` for CANN-9.0.0 AICPU mode. |
| NDS AIV object | Verbs, connection, and storage APIs plus a direct `*Op` kernel entry for each | Built as one CCEC translation unit because CANN 9.0.0 rejects a multi-object AIV image | Lets ACL select one concrete operation with no NDS-level dispatch. |
| NDS AICPU package | Exported verbs, connection, and storage APIs plus a direct `*Op` entry for each | Built as a standard-CP1 shared object and loaded through ACL mode 0 | Lets ACL select one concrete operation with no NDS-level dispatch. |
| `libhcomm.so`, HCCL | none | No loader or wrapper is built | Their communicator, bundled Tx/Rx kernels, rank state, and peer protocols are outside the direct CPU-peer topology. |

### Dynamic-loader invariants

1. All CANN libraries used by one NPU process must come from one selected CANN release.
2. Resolve required symbols with `dlsym` before executing hardware work; fail closed on a missing ABI entry point.
3. Do not assume an open-source HCOMM checkout has exactly the same symbols as the installed CANN package.
4. Keep ABI-facing structs internal to the loader boundary and exchange only
   NDS-owned transport and storage records with the CPU process.
5. The CPU verbs executable must never link or load CANN.
6. `--execution aicpu` loads only the NDS-built standard-CP1 package. NDS does not build or expose a custom-process AICPU mode because CANN does not publish the RNIC mapping/import contract that it would require.
7. CANN 9.0.0 provides an AICPU loader but no public device-side RNIC post API. The NDS-owned AICPU package is a normal aarch64 DYN shared object, not a `bisheng -x aicpu` relocatable object. It confines the version-coupled HNS provider ABI to `src/client/execution/aicpu/device/hns_abi.h` and dynamically resolves `libhns-rdmav25.so:ibv_exp_post_send` inside the AICPU environment. The host process never loads that provider.
8. The CPU polls its verbs CQ for command Receive and terminal completion Write. The current host StorageClient observes the CPU-written completion record through an AscendCL device-to-host copy in every execution mode. ACL synchronization, HCCP AI-QP CQ handling, and an RA local CQE are not NDS storage completion.
9. HCOMM's bundled `RunTransportRoceTx` path is reference material, not a fallback. It depends on matching Rx-side transport logic, reciprocal flag buffers, and HCOMM synchronization semantics that the CPU verbs server does not implement. NDS therefore has no HCOMM loader, communicator bootstrap, or bundled-kernel wrapper.

### Validated RA subset

The verified bounded NPU-device-memory RDMA Write dynamically uses AscendCL, CANN runtime, and `libra.so` for:

```text
aclInit → aclrtSetDevice → rtOpenNetService → RaInit
→ RaRdevInitV2 → RaTypicalQpCreate → RaGetQpAttr → endpoint exchange
→ RaTypicalQpModify → RaRegisterMr → RaTypicalSendWr(SEND)
→ rtRDMADBSend → completion-record poll → RaDeregisterMr
→ RaQpDestroy → RaRdevDeinit → RaDeinit → rtCloseNetService → aclFinalize
```

The matching HCCP reference source establishes that the offline HDC rdev path requires `NOTIFY` (`1`) for rdev creation and destruction. NDS uses the CANN 9.0.0 `RaRdevInitV2` entry point and retains the RA Lite context used by the validated lifecycle. For OPBASE Lite QPs, `RaTypicalSendWr` returns runtime doorbell information but does not ring it; the HCOMM OPBASE transport then calls `hrtRDMADBSend`, which NDS maps to dynamically resolved `rtRDMADBSend` after re-selecting the logical device.

AICPU mode builds and loads NDS's standard-CP1 package and does not call `RaPollCq` for its AI QP. Provider resolution occurs only inside CP1; host `ldconfig`, host filesystem searches, and host-process `dlopen` are not valid provider tests. CANN 9.0.0 does not expose the doorbell mapping/import contract needed by an independently launched custom AICPU process, so NDS does not build one.

The same source basis fixes the CPU-side path-MTU policy: HCOMM's `RsDrvQpStateModifytoRtr` selects `IBV_QP_PATH_MTU` from the local active port through `RsDrvSetMtu`; its `TypicalQp` ABI has no path-MTU field. NDS therefore treats the peer MTU record as diagnostic rather than constraining the CPU QP with it.

### Standard-CP1 AICPU subset

The NDS-owned optional AICPU Tx path has a deliberately narrow lifecycle:

```text
aclInit → aclrtSetDevice → rtOpenNetService → RaInit
→ RaRdevInitV2 → RaAiQpCreate(NORMAL)
→ endpoint exchange → RaTypicalQpModify → register application, command, and completion MRs
→ aclrtBinaryLoadFromFile(nds_aicpu_standard.json, CPU_KERNEL_MODE=0)
→ NdsAicpuRdma* → NdsAicpuRdma*Impl → NdsAicpuPost*/PollCqImpl
→ aclrtSynchronizeStreamWithTimeout
→ CPU writes terminal completion record → host polls completion record
→ unload package/destroy stream → deregister MR → teardown
```

The device entry point contains no copied HCOMM implementation. It validates
the versioned NDS device-operation request, dynamically opens the CANN-9.0.0
HNS provider through device-side `dlopen`/`dlsym`, and resolves
`ibv_exp_post_send`. It probes `ibv_post_recv` and `ibv_poll_cq` too, then uses
the NDS queue-address fallback when those inline verbs operations are not
exported. NDS creates an AI NORMAL QP by default because the provider rings
`sq.db_reg` itself in that mode. NDS does not adopt HCOMM's KFC dispatcher,
flag buffers, peer waits, batch/split flows, rank state, retry protocol, or
background event poller.

## Scope and references

The resource lifecycle is based on the CANN 9.0.0 HCCP implementation in the
matching HCOMM source tree, particularly `src/platform/hccp`, and is implemented
by `NpuRaContext` and `NpuRaQp`. NDS dynamically loads the required RA/runtime
ABI and does not link or copy HCCP implementation code.
