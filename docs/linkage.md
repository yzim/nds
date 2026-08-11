# NDS linkage policy

NDS makes a per-library ABI decision. Stable platform and CPU verbs APIs are linked normally; version-coupled CANN surfaces are isolated behind narrow runtime loaders unless a separately configured, version-pinned public API build is selected.

## Direct one-NPU / one-CPU path

The production interoperability path does **not** use HCOMM, HCCL, TSD, a rank table, or another NPU. It establishes one NPU RA QP against one CPU `libibverbs` QP.

| Library | NDS use | Build treatment | Rationale |
|---|---|---|---|
| POSIX / `libdl` / threads | sockets, control plane, dynamic loading | Link | Stable system ABI. |
| `libibverbs.so` | CPU-side PD/CQ/QP operations | Link, CPU target only | The CPU endpoint remains CANN-free. |
| `libascendcl.so` | `aclInit`, `aclFinalize`, `aclrtSetDevice` | Dynamic by default; optional version-pinned public-ACL link mode | Public lifecycle boundary; dynamic default preserves host build portability. |
| `libruntime.so` | `rtOpenNetService`, `rtCloseNetService`, `rtRDMADBSend` | Runtime-load | Required by the direct NPU RA lifecycle and OPBASE Lite doorbell submission; not exposed through a stable NDS-facing SDK contract. |
| `libra.so` | `RaInit`, rdev/QP lifecycle, MR registration, send/CQ APIs | Runtime-load | Required ABI is private/version-coupled; NDS transcribes only the interfaces it uses. |
| `libhcomm.so`, HCCL, TSD | none in the direct path | Do not load for normal QP establishment | They are not required for the CPU-peer topology and add unwanted multi-rank/global lifecycle ownership. |

## Reference boundary

We thank the maintainers and contributors of the open-source [HCCL](https://gitcode.com/cann/hccl) and [HCOMM](https://gitcode.com/cann/hcomm) projects. The technical basis for NDS lifecycle, transport, and ABI validation is based on information already publicly available through those projects. HCCP source under HCOMM is especially relevant to RA. The installed CANN shared objects are authoritative at runtime.

## Dynamic-loader invariants

1. All CANN libraries used by one NPU process must come from one selected CANN release.
2. Resolve required symbols with `dlsym` before executing hardware work; fail closed on a missing ABI entry point.
3. Do not assume an open-source HCOMM checkout has exactly the same symbols as the installed CANN package.
4. Keep ABI-facing structs internal to the loader boundary and exchange only NDS wire records with the CPU process.
5. The CPU verbs executable must never link or load CANN.

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

This completion policy applies only to the host-RA submission mode. An AICPU-submission mode must have a separately validated, single-owner completion design before it is enabled; NDS will not reuse HCOMM's packaged CCL AICPU kernel or assume its private parameter/completion ABI.

The same source basis fixes the CPU-side path-MTU policy: HCOMM's `RsDrvQpStateModifytoRtr` selects `IBV_QP_PATH_MTU` from the local active port through `RsDrvSetMtu`; its `TypicalQp` ABI has no path-MTU field. NDS therefore treats the peer MTU record as diagnostic rather than constraining the CPU QP with it.
