# Reference Basis

NDS uses public source material to understand lifecycle, ABI intent, queue
layout, ordering, and ownership. The installed CANN libraries on the target are
authoritative at runtime; reference source never proves identical symbols or
behavior in an installed binary.

## Evidence rules

Use this order of evidence for a change:

1. Bounded target experiment for a runtime behavior or completion claim.
2. Installed headers, exported symbols, the matching CANN `set_env.sh`, and
   loader checks for ABI availability.
3. Matching CANN 9.0.0 and HCOMM source for lifecycle and data-layout intent.
4. Upstream rdma-core and public Ascend documentation for generic concepts.

Do not copy, vendor, or link private CANN, HCCP, HCOMM, HCCL, or HNS provider
implementation code. Keep vendor ABI use behind NDS loaders and the AICPU
device boundary.

## Primary sources

- [HCOMM](https://gitcode.com/cann/hcomm): HCCP lifecycle, RA/AI-QP intent,
  queue and doorbell behavior, and the limits of bundled transport.
- [HCCL](https://gitcode.com/cann/hccl): communication terminology and public
  integration context; it is not part of the NDS CPU-peer topology.
- [rdma-core](https://github.com/linux-rdma/rdma-core): standard verbs and
  provider patterns. The patched HNS provider is relevant only to the narrow
  AICPU device-side post boundary.
- Public Ascend/CANN documentation and headers: AscendCL lifecycle, binary and
  CPU-kernel package loading, and public toolchain behavior.

Open-source HCOMM transport is not an NDS fallback. It assumes a reciprocal
HCOMM peer, communicator state, flag buffers, and synchronization semantics
that NDS intentionally does not implement.

Current target evidence records the bounded verbs correctness workflow. The
transport implementation has a bounded request baseline, but its focused target
validation, queue-credit behavior, and throughput are still open. The evidence
also does not establish a complete verbs error-propagation contract or verbs
performance baseline. Those claims require separate, bounded experiments
recorded under ignored `.local/` state.

For the resulting NDS contracts, see [design](design.md). Future work and its
required validation belong in [the roadmap](roadmap.md).
