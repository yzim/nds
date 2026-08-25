# Reference Basis

NDS uses public source material to understand lifecycle, ABI intent, queue
layout, ordering, and ownership. The installed CANN libraries on the target are
authoritative at runtime; reference source never proves identical symbols or
behavior in an installed binary.

## Evidence rules

Use this order of evidence for a change:

1. Bounded target experiment for a runtime behavior or completion claim.
2. Installed headers, exported symbols, and loader checks for ABI availability.
3. Matching CANN 9.0.0 and HCOMM source for lifecycle and data-layout intent.
4. Upstream rdma-core and public Ascend documentation for generic concepts.

## Primary sources

- [HCOMM](https://gitcode.com/cann/hcomm): HCCP lifecycle, RA/AI-QP intent,
  queue and doorbell behavior, and the limits of bundled transport.
  QP-mode provenance for this repository: `TypicalQpManager::CreateQp` uses
  OPBASE; `TransportDirectNpu::GetQpMode` selects OPBASE_EXT on 910B/910_93
  for operator-based and AICPU-enabled paths; and `TransportManager` overrides
  AIV direct RoCE to NORMAL to avoid STARS submission. These are source-intent
  references, not a claim about an installed provider binary.
- HCOMM's bundled HNS provider source: `hns_roce_u_ext_post_send` updates the
  SQ doorbell for NORMAL AI-QPs, while non-normal AI-QP paths return `db_info`
  for a separate runtime/stream submission. HCOMM combines that result with
  the AI-QP doorbell index and submits it through its dispatcher. This supports
  the NDS AIV result that grouped doorbell ringing is determined by who owns
  the WQE/SQ-doorbell writes, not by the numeric QP mode alone: AIV directly
  writes both, so its tested NORMAL and OPBASE_EXT QPs can use the same grouped
  doorbell mechanism. It does not imply that those modes have interchangeable
  provider or runtime contracts, and remains source-basis evidence rather than
  an installed-provider guarantee.
- HCOMM doorbell-to-stream source trace distinguishes three submission models.
  Its typical RA `OPBASE` executor receives `{dbIndex, dbInfo}` from
  `HrtRaSendWrV2` and passes them, with the caller's `rtStream_t`, to the
  public `rtRDMADBSend`; its multi-WQE path retains the final response and
  calls that API once per doorbell group. Therefore an NDS RA connection can
  use a caller-provided runtime stream and the public prepare-then-ring model.
  Its AICPU AI-QP transport instead gives the provider-returned descriptor to
  `DispatcherAiCpu::RdmaSend`, which appends a private `RDMA_DB_SEND_SQE` to
  the selected stream's RTSQ buffer; the runtime STARS engine executes that
  entry as the RNIC doorbell write. HCOMM graph mode is another private path:
  `DispatcherGraph::RdmaSend` creates `GraphAddRdmaSendTask` nodes. NDS must
  not depend on either private AICPU RTSQ or graph mechanism. The public
  `rtRDMADBSend` API accepts a stream, but its compatibility with user graph
  capture is not yet validated and must be tested before NDS makes that claim.
- [HCCL](https://gitcode.com/cann/hccl): communication terminology and public
  integration context; it is not part of the NDS CPU-peer topology.
- [rdma-core](https://github.com/linux-rdma/rdma-core): standard verbs and
  provider patterns. The patched HNS provider is relevant only to the narrow
  CP1 device-side post boundary.
- Public Ascend/CANN documentation and headers: AscendCL lifecycle, binary and
  CPU-kernel package loading, and public toolchain behavior.

Open-source HCOMM transport is not an NDS fallback. It assumes a reciprocal
HCOMM peer, communicator state, flag buffers, and synchronization semantics
that NDS intentionally does not implement.

For the resulting NDS contracts, see [design](design.md). Future work and its
required validation belong in [the roadmap](roadmap.md).
