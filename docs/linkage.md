# NDS linkage policy

NDS makes a per-library ABI decision. Stable platform and CPU verbs APIs are linked normally; version-coupled CANN surfaces are isolated behind narrow runtime loaders unless a separately configured, version-pinned public API build is selected.

## Direct one-NPU / one-CPU path

The production interoperability path does **not** use HCOMM, HCCL, TSD, a rank table, or another NPU. It establishes one NPU RA QP against one CPU `libibverbs` QP.

| Library | NDS use | Build treatment | Rationale |
|---|---|---|---|
| POSIX / `libdl` / threads | sockets, control plane, dynamic loading | Link | Stable system ABI. |
| `libibverbs.so` | CPU-side PD/CQ/QP operations | Link, CPU target only | The CPU endpoint remains CANN-free. |
| `libascendcl.so` | lifecycle plus the AICPU binary/argument/stream APIs when selected | Dynamic by default; optional version-pinned public-ACL link mode | Public lifecycle boundary; dynamic default preserves host build portability. |
| `libruntime.so` | `rtOpenNetService`, `rtCloseNetService`, `rtRDMADBSend` | Runtime-load | Required by the direct NPU RA lifecycle and default OPBASE Lite doorbell submission; not exposed through a stable NDS-facing SDK contract. |
| `libra.so` | `RaInit`, rdev/QP lifecycle, MR registration, send/CQ APIs | Runtime-load | Required ABI is private/version-coupled; NDS transcribes only the interfaces it uses, including optional `RaAiQpCreate` for CANN-9.0.0 AICPU mode. |
| CANN AICPU CCL package | `RunTransportRoceTx` from a caller-selected `ccl_kernel.json` | Dynamic load at runtime, AICPU mode only | Vendor-installed CANN-9.0.0 payload; never copied or linked into NDS. |
| `libhcomm.so`, HCCL, TSD | none in the direct path | Do not load for normal QP establishment | They are not required for the CPU-peer topology and add unwanted multi-rank/global lifecycle ownership. |

## Reference boundary

We thank the maintainers and contributors of the open-source [HCCL](https://gitcode.com/cann/hccl) and [HCOMM](https://gitcode.com/cann/hcomm) projects. The technical basis for NDS lifecycle, transport, and ABI validation is based on information already publicly available through those projects. HCCP source under HCOMM is especially relevant to RA. The installed CANN shared objects are authoritative at runtime.

## Dynamic-loader invariants

1. All CANN libraries used by one NPU process must come from one selected CANN release.
2. Resolve required symbols with `dlsym` before executing hardware work; fail closed on a missing ABI entry point.
3. Do not assume an open-source HCOMM checkout has exactly the same symbols as the installed CANN package.
4. Keep ABI-facing structs internal to the loader boundary and exchange only NDS wire records with the CPU process.
5. The CPU verbs executable must never link or load CANN.
6. `--submission-mode aicpu` is CANN-9.0.0 package ABI-pinned. It dynamically loads the caller-selected kernel configuration, requires the installed `RunTransportRoceTx` function, and fails before posting data if any ACL or RA AI-QP capability is absent.
7. The AICPU mode uses a project-owned 24-byte sync MR at each endpoint. The CPU exports its descriptor only when invoked with `--aicpu-sync`; this does not change the CPU linkage model.
8. Completion ownership is mode-specific: default host submission uses explicit `RaPollCq`; AICPU submission uses ACL stream synchronization and never consumes that CQ through host `RaPollCq`.

## Validated RA subset

The verified bounded NPU-device-memory RDMA Write dynamically uses AscendCL, CANN runtime, and `libra.so` for:

```text
aclInit → aclrtSetDevice → rtOpenNetService → RaInit
→ RaRdevInitV2 → RaTypicalQpCreate → RaGetQpAttr → endpoint exchange
→ RaTypicalQpModify → RaRegisterMr → RaTypicalSendWr
→ rtRDMADBSend → RaPollCq → RaDeregisterMr
→ RaQpDestroy → RaRdevDeinit → RaDeinit → rtCloseNetService → aclFinalize
```

The matching HCCP reference source establishes that the offline HDC rdev path requires `NOTIFY` (`1`) for rdev creation and destruction. NDS uses the CANN 9.0.0 `RaRdevInitV2` entry point with `disabledLiteThread=true`. The legacy `RaRdevInit` hard-codes that field false and starts HCOMM's background Lite-CQ polling thread; because NDS calls `RaPollCq` after its signaled write, allowing both consumers would make completion ownership racy. For OPBASE Lite QPs, `RaTypicalSendWr` returns the runtime doorbell information but does not ring it; the HCOMM OPBASE transport then calls `hrtRDMADBSend`, which NDS maps to dynamically resolved `rtRDMADBSend` after re-selecting the logical device.

This host-CQ policy applies only to the host-RA submission mode. The experimental AICPU mode instead dynamically loads CANN 9.0.0's installed CCL AICPU package after validating the `RunTransportRoceTx` entry point and the source-derived parameter ABI. Its dedicated ACL stream is the sole completion owner; it does not enable the Lite-CQ poller or call `RaPollCq`. Hardware data-path acceptance for that mode remains pending.

The same source basis fixes the CPU-side path-MTU policy: HCOMM's `RsDrvQpStateModifytoRtr` selects `IBV_QP_PATH_MTU` from the local active port through `RsDrvSetMtu`; its `TypicalQp` ABI has no path-MTU field. NDS therefore treats the peer MTU record as diagnostic rather than constraining the CPU QP with it.


## Experimental AICPU Tx subset

The optional AICPU Tx path follows the CANN 9.0.0 HCOMM reference behavior while retaining NDS ownership of all process and control-plane lifecycle:

```text
aclInit → aclrtSetDevice → rtOpenNetService → RaInit
→ RaRdevInitV2(disabledLiteThread=true) → RaAiQpCreate(OPBASE_EXT)
→ endpoint exchange → RaTypicalQpModify
→ register source MR + local 24-byte sync MR
→ aclrtBinaryLoadFromFile(ccl_kernel.json)
→ RunTransportRoceTx → aclrtSynchronizeStreamWithTimeout
→ unload package/destroy stream → deregister MRs → teardown
```

The CPU peer is unchanged except for optional allocation/registration and exchange of its 24-byte sync MR. This is an implementation and build/unit-test milestone; a hardware NPU-to-CPU payload/guard validation is still required before classifying this route as verified.
