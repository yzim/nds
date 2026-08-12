# NDS linkage policy

NDS makes a per-library ABI decision. Stable platform and CPU verbs APIs are linked normally; version-coupled CANN surfaces are isolated behind narrow runtime loaders unless a separately configured, version-pinned public API build is selected.

## Direct one-NPU / one-CPU path

The production interoperability path does **not** use HCOMM, HCCL, TSD, a rank table, or another NPU. It establishes one NPU RA QP against one CPU `libibverbs` QP.

| Library | NDS use | Build treatment | Rationale |
|---|---|---|---|
| POSIX / `libdl` / threads | sockets, control plane, dynamic loading | Link | Stable system ABI. |
| `libibverbs.so` | CPU-side PD/CQ/QP operations | Link, CPU target only | The CPU endpoint remains CANN-free. |
| `libascendcl.so` | lifecycle plus AIV/AICPU binary, argument, kernel, and stream APIs when selected | Dynamic by default; optional version-pinned public-ACL link mode | Public lifecycle boundary; dynamic default preserves host build portability. |
| `libruntime.so` | `rtOpenNetService`, `rtCloseNetService`, `rtRDMADBSend` | Runtime-load | Required by the direct NPU RA lifecycle and default OPBASE Lite doorbell submission; not exposed through a stable NDS-facing SDK contract. |
| `libra.so` | `RaInit`, rdev/QP lifecycle, MR registration, send/CQ APIs | Runtime-load | Required ABI is private/version-coupled; NDS transcribes only the interfaces it uses, including optional `RaAiQpCreate` for CANN-9.0.0 AICPU mode. |
| NDS AIV object | `NdsAivRdmaWrite` from the CCEC-built `nds_aiv_roce.o` | Built from this repository with the selected CANN toolchain and loaded through ACL | Owns the minimal direct HNS SQ/WQE/doorbell implementation. It does not load HCOMM. |
| NDS AICPU package | `NdsAicpuRdmaPost` from the NDS standard-CP1 package | Built from this repository on the selected aarch64 CANN host; installed as a customer AICPU package and loaded through ACL mode 0 | Owns a minimal one-WR RDMA-post kernel. It neither vendors nor loads HCOMM payloads. |
| `libhcomm.so`, HCCL | none | No loader or wrapper is built | Their communicator, bundled Tx/Rx kernels, rank state, and peer protocols are outside the direct CPU-peer topology. |
| TSD client | capability diagnostics only | Runtime-load only in the standalone probe | It is not initialized or loaded by any submission mode. |

## Reference boundary

We thank the maintainers and contributors of the open-source [HCCL](https://gitcode.com/cann/hccl) and [HCOMM](https://gitcode.com/cann/hcomm) projects. The technical basis for NDS lifecycle, transport, and ABI validation is based on information already publicly available through those projects. HCCP source under HCOMM is especially relevant to RA. The installed CANN shared objects are authoritative at runtime.

## Dynamic-loader invariants

1. All CANN libraries used by one NPU process must come from one selected CANN release.
2. Resolve required symbols with `dlsym` before executing hardware work; fail closed on a missing ABI entry point.
3. Do not assume an open-source HCOMM checkout has exactly the same symbols as the installed CANN package.
4. Keep ABI-facing structs internal to the loader boundary and exchange only NDS wire records with the CPU process.
5. The CPU verbs executable must never link or load CANN.
6. `--submission-mode aicpu` performs real submission only with the NDS-built standard package and `--aicpu-cpu-kernel-mode 0`. Mode 1 is diagnostic-only and fails closed for real submission because its separate custom process has no supported import of CP1's RNIC doorbell mapping.
7. CANN 9.0.0 provides an AICPU loader but no public device-side RNIC post API. The NDS-owned AICPU package is a normal aarch64 DYN shared object (built by the same shared-library model as CANN's custom-kernel template), not a `bisheng -x aicpu` relocatable object. It confines the version-coupled HNS provider ABI to `src/nic_client/modes/aicpu/device/include/nds_aicpu_hns_abi.h` and dynamically resolves `libhns-rdmav25.so:ibv_exp_post_send` through `dlopen` / `dlsym` from inside the AICPU/NPU execution environment. This matches HCOMM's device-side `DlHnsFunction` path (`HcclDlopen` is a wrapper around `dlopen`) beneath `TransportDeviceIbverbs::HnsPostSend`. The host launcher does not link, load, probe, or make availability claims about that device-side provider; it only loads NDS's kernel package through ACL.
8. Completion ownership is mode-specific: host submission explicitly uses `RaPollCq`; HCCP owns AIV/AICPU AI-QP CQs, so those modes do not call it. ACL kernel synchronization proves kernel return, not RNIC completion. NDS keeps resources alive until the CPU verifies the remote effect and acknowledges the transaction.
9. HCOMM's bundled `RunTransportRoceTx` path is reference material, not a fallback. It depends on matching Rx-side transport logic, reciprocal flag buffers, and HCOMM synchronization semantics that the CPU verbs server does not implement. NDS therefore has no HCOMM loader, communicator bootstrap, or bundled-kernel wrapper.

## Validated RA subset

The verified bounded NPU-device-memory RDMA Write dynamically uses AscendCL, CANN runtime, and `libra.so` for:

```text
aclInit → aclrtSetDevice → rtOpenNetService → RaInit
→ RaRdevInitV2 → RaTypicalQpCreate → RaGetQpAttr → endpoint exchange
→ RaTypicalQpModify → RaRegisterMr → RaTypicalSendWr
→ rtRDMADBSend → RaPollCq → RaDeregisterMr
→ RaQpDestroy → RaRdevDeinit → RaDeinit → rtCloseNetService → aclFinalize
```

The matching HCCP reference source establishes that the offline HDC rdev path requires `NOTIFY` (`1`) for rdev creation and destruction. NDS uses the CANN 9.0.0 `RaRdevInitV2` entry point and retains the RA Lite context used by the validated lifecycle. For OPBASE Lite QPs, `RaTypicalSendWr` returns runtime doorbell information but does not ring it; the HCOMM OPBASE transport then calls `hrtRDMADBSend`, which NDS maps to dynamically resolved `rtRDMADBSend` after re-selecting the logical device. The application remains the only explicit `RaPollCq` caller in host mode.

This host-CQ policy applies only to host-RA submission. AICPU mode builds and loads NDS's standard-CP1 package and never calls `RaPollCq` for its AI QP. Provider resolution occurs only inside CP1; host `ldconfig`, host filesystem searches, and host-process `dlopen` are not valid provider tests. CANN 9.0.0 does not expose the doorbell mapping/import contract required by an independently launched mode-1 custom process, which is why real submission is restricted to standard mode 0.

The same source basis fixes the CPU-side path-MTU policy: HCOMM's `RsDrvQpStateModifytoRtr` selects `IBV_QP_PATH_MTU` from the local active port through `RsDrvSetMtu`; its `TypicalQp` ABI has no path-MTU field. NDS therefore treats the peer MTU record as diagnostic rather than constraining the CPU QP with it.


## Standard-CP1 AICPU subset

The NDS-owned optional AICPU Tx path has a deliberately narrow lifecycle:

```text
aclInit → aclrtSetDevice → rtOpenNetService → RaInit
→ RaRdevInitV2 → RaAiQpCreate(NORMAL)
→ endpoint exchange → RaTypicalQpModify → register source MR
→ aclrtBinaryLoadFromFile(nds_aicpu_standard.json, CPU_KERNEL_MODE=0)
→ NdsAicpuRdmaPost → aclrtSynchronizeStreamWithTimeout
→ CPU transaction verification
→ unload package/destroy stream → deregister MR → teardown
```

The device entry point contains no copied HCOMM implementation. It validates NDS's 80-byte v6 request, dynamically opens the CANN-9.0.0 HNS provider through device-side `dlopen`/`dlsym`, and resolves `ibv_exp_post_send`. NDS creates an AI NORMAL QP because the provider rings `sq.db_reg` itself only in that mode. OPBASE_EXT is normalized to provider OP mode and would require separate dispatcher submission of the returned `db_info`. The successful standard-CP1 Write advanced CPU receive PSN by one and passed payload and guard verification. NDS does not adopt HCOMM's KFC dispatcher, flag buffers, peer waits, batch/split flows, rank state, retry protocol, or background event poller.

See [NDS RDMA submission modes](submission-modes.md) for the mode comparison and detailed guides.
