# Runtime Libraries and ABI

NDS makes one explicit decision per library. Stable platform and CPU verbs APIs
are linked normally. Version-coupled CANN interfaces stay behind narrow runtime
loaders, except for the optional version-pinned public AscendCL build.

## Direct one-NPU / one-CPU path

The production interoperability path does **not** use HCOMM, HCCL, TSD, a rank table, or another NPU. It establishes one NPU RA QP against one CPU `libibverbs` QP.

| Library | NDS use | Build treatment | Rationale |
|---|---|---|---|
| POSIX / `libdl` / threads | sockets, TCP transport bootstrap, dynamic loading | Link | Stable system ABI. |
| `libibverbs.so` | CPU-side PD/CQ/QP operations | Link, CPU target only | The CPU endpoint remains CANN-free. |
| `libascendcl.so` | lifecycle plus AIV/AICPU binary, argument, kernel, and stream APIs when selected | Dynamic by default; optional version-pinned public-ACL link mode | Public lifecycle boundary; dynamic default preserves host build portability. |
| `libruntime.so` | `rtOpenNetService`, `rtCloseNetService`, `rtRDMADBSend` | Runtime-load | Required by the direct NPU RA lifecycle and default OPBASE Lite doorbell ring; not exposed through a stable NDS-facing SDK contract. |
| `libra.so` | `RaInit`, rdev/QP lifecycle, MR registration, send/CQ APIs | Runtime-load | Required ABI is private/version-coupled; NDS transcribes only the interfaces it uses, including optional `RaAiQpCreate` for CANN-9.0.0 AICPU mode. |
| NDS AIV object | Verbs/connection functions plus `NdsAivConnectionOp` in `nds_aiv_kernel.o` | Built as one CCEC translation unit because CANN 9.0.0 rejects a multi-object AIV image; other operators compile the reusable dataplane source against its API header | Keeps the callable APIs separate in source while respecting the loader format. |
| NDS AICPU package | Exported verbs/connection symbols plus the `NdsAicpuConnectionOp` dispatch entry | Built as a standard-CP1 shared object and loaded through ACL mode 0 | Lets other AICPU operators invoke the data plane without routing through the host-launch entry. |
| `libhcomm.so`, HCCL | none | No loader or wrapper is built | Their communicator, bundled Tx/Rx kernels, rank state, and peer protocols are outside the direct CPU-peer topology. |

## Dynamic-loader invariants

1. All CANN libraries used by one NPU process must come from one selected CANN release.
2. Resolve required symbols with `dlsym` before executing hardware work; fail closed on a missing ABI entry point.
3. Do not assume an open-source HCOMM checkout has exactly the same symbols as the installed CANN package.
4. Keep ABI-facing structs internal to the loader boundary and exchange only
   NDS-owned transport and storage records with the CPU process.
5. The CPU verbs executable must never link or load CANN.
6. `--execution aicpu` loads only the NDS-built standard-CP1 package. NDS does not build or expose a custom-process AICPU mode because CANN does not publish the RNIC mapping/import contract that it would require.
7. CANN 9.0.0 provides an AICPU loader but no public device-side RNIC post API. The NDS-owned AICPU package is a normal aarch64 DYN shared object, not a `bisheng -x aicpu` relocatable object. It confines the version-coupled HNS provider ABI to `src/client/data_plane/aicpu/device/include/nds_aicpu_hns_abi.h` and dynamically resolves `libhns-rdmav25.so:ibv_exp_post_send` inside the AICPU environment. The host process never loads that provider.
8. The CPU polls its verbs CQ for command Receive and terminal completion Write. The current host StorageClient observes the CPU-written completion record through an AscendCL device-to-host copy in every execution mode. ACL synchronization, HCCP AI-QP CQ handling, and a host-RA local CQE are not NDS storage completion.
9. HCOMM's bundled `RunTransportRoceTx` path is reference material, not a fallback. It depends on matching Rx-side transport logic, reciprocal flag buffers, and HCOMM synchronization semantics that the CPU verbs server does not implement. NDS therefore has no HCOMM loader, communicator bootstrap, or bundled-kernel wrapper.

## Validated RA subset

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


## Standard-CP1 AICPU subset

The NDS-owned optional AICPU Tx path has a deliberately narrow lifecycle:

```text
aclInit → aclrtSetDevice → rtOpenNetService → RaInit
→ RaRdevInitV2 → RaAiQpCreate(NORMAL)
→ endpoint exchange → RaTypicalQpModify → register application, command, and completion MRs
→ aclrtBinaryLoadFromFile(nds_aicpu_standard.json, CPU_KERNEL_MODE=0)
→ NdsAicpuConnectionOp → NdsAicpuRdma* → NdsAicpuPost*/PollCq
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

See [NPU execution modes](npu-backends.md) for the execution comparison and detailed guides.
