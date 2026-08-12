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
| NDS AICPU package | `NdsAicpuRdmaPost` from an NDS-built `nds_aicpu_roce.json` | Built from this repository on the selected aarch64 CANN host; dynamic load at runtime | Owns a minimal one-WR RDMA-post kernel. It neither vendors nor loads HCOMM payloads. |
| `libhcomm.so`, HCCL, TSD | none in the direct path | Do not load for normal QP establishment | They are not required for the CPU-peer topology and add unwanted multi-rank/global lifecycle ownership. |

## Reference boundary

We thank the maintainers and contributors of the open-source [HCCL](https://gitcode.com/cann/hccl) and [HCOMM](https://gitcode.com/cann/hcomm) projects. The technical basis for NDS lifecycle, transport, and ABI validation is based on information already publicly available through those projects. HCCP source under HCOMM is especially relevant to RA. The installed CANN shared objects are authoritative at runtime.

## Dynamic-loader invariants

1. All CANN libraries used by one NPU process must come from one selected CANN release.
2. Resolve required symbols with `dlsym` before executing hardware work; fail closed on a missing ABI entry point.
3. Do not assume an open-source HCOMM checkout has exactly the same symbols as the installed CANN package.
4. Keep ABI-facing structs internal to the loader boundary and exchange only NDS wire records with the CPU process.
5. The CPU verbs executable must never link or load CANN.
6. `--submission-mode aicpu` loads the caller-selected **NDS-built** `nds_aicpu_roce.json`, requires `NdsAicpuRdmaPost`, and fails before posting data if any ACL or RA AI-QP capability is absent.
7. CANN 9.0.0 provides an AICPU loader but no public device-side RNIC post API. The NDS-owned custom AICPU package is a normal aarch64 DYN shared object (built by the same shared-library model as CANN's custom-kernel template), not a `bisheng -x aicpu` relocatable object. It confines the version-coupled HNS provider ABI to `aicpu/include/nds_aicpu_hns_abi.h` and dynamically resolves `libhns-rdmav25.so:ibv_exp_post_send` through `dlopen` / `dlsym` from inside the AICPU/NPU execution environment. This matches HCOMM's device-side `DlHnsFunction` path (`HcclDlopen` is a wrapper around `dlopen`) beneath `TransportDeviceIbverbs::HnsPostSend`. The host launcher does not link, load, probe, or make availability claims about that device-side provider; it only loads NDS's kernel package through ACL.
8. Completion ownership is mode-specific: default host submission uses explicit `RaPollCq`; AICPU submission uses ACL stream synchronization and never consumes that CQ through host `RaPollCq`. Stream completion confirms local AICPU execution/WQE post, while CPU payload visibility is verified by the CPU endpoint after control-plane close.

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

This host-CQ policy applies only to the host-RA submission mode. The experimental AICPU mode builds and dynamically loads NDS's own package, validates the `NdsAicpuRdmaPost` entry point and NDS request ABI, and uses its dedicated ACL stream as sole local completion owner. It does not enable the Lite-CQ poller or call `RaPollCq`. Its provider resolution occurs only inside the AICPU/NPU execution environment; host `ldconfig`, host filesystem searches, and host-process `dlopen` are not valid tests of that provider. Hardware data-path acceptance remains pending.

The same source basis fixes the CPU-side path-MTU policy: HCOMM's `RsDrvQpStateModifytoRtr` selects `IBV_QP_PATH_MTU` from the local active port through `RsDrvSetMtu`; its `TypicalQp` ABI has no path-MTU field. NDS therefore treats the peer MTU record as diagnostic rather than constraining the CPU QP with it.


## Experimental AICPU Tx subset

The NDS-owned optional AICPU Tx path has a deliberately narrow lifecycle:

```text
aclInit → aclrtSetDevice → rtOpenNetService → RaInit
→ RaRdevInitV2(disabledLiteThread=true) → RaAiQpCreate(OPBASE_EXT)
→ endpoint exchange → RaTypicalQpModify → register source MR
→ aclrtBinaryLoadFromFile(nds_aicpu_roce.json)
→ NdsAicpuRdmaPost → aclrtSynchronizeStreamWithTimeout
→ unload package/destroy stream → deregister MR → teardown
```

The device entry point contains no copied HCOMM implementation. It validates NDS's 80-byte v3 request, dynamically opens the CANN-9.0.0 HNS provider through its device-side `dlopen`/`dlsym` loader, resolves `ibv_exp_post_send`, constructs exactly one signaled RDMA Write, RDMA Read, or Send WQE, and issues the required store barrier. It then dynamically resolves CANN's already-installed AICPU-side `hrtRDMADBSend` wrapper from `libaicpu_custom.so` and attempts to submit the provider-returned `db_info` with the AI-QP's `db_index`. CANN exposes no public custom-kernel stream-handle ABI for that wrapper, so the kernel passes a null stream; its acceptability is explicitly target-hardware validation work, not an assumed contract. This is only the minimal post/doorbell sequence; it does not adopt HCOMM's KFC dispatcher, flag buffers, peer waits, batch/split flows, rank state, retry protocol, or background event poller. An optional NDS-owned device-memory checkpoint makes kernel entry, provider load/post, and doorbell failure observable to the host; it is diagnostic only and is not a peer synchronization mechanism. The current CPU peer exports only an ordinary remote-write destination MR, so the launcher currently exercises Write. Generic hardware acceptance remains pending.
