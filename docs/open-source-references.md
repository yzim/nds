# Open-Source Reference Basis

NDS was designed using publicly available source code. It does not link, copy,
vendor, or redistribute HCOMM, HCCL, an HNS provider, a kernel driver, or their
implementation code. The production path remains one NPU RA endpoint and one
CPU `libibverbs` endpoint; it does not initialize HCOMM, HCCL, or a collective
job.

This document records which sources inform NDS design decisions and the limits
of each source. The selected CANN installation, matched driver, resolved
runtime symbols, and bounded experiment on the target remain authoritative for
runtime behavior.

## Evidence classes

| Class | May establish | Cannot establish |
|---|---|---|
| Matching open-source implementation | Expected lifecycle, ABI intent, queue layout, ordering, and ownership model | That the installed binary has identical symbols or behavior |
| Upstream implementation | Generic verbs and HNS design context | Ascend-specific extensions or target-driver compatibility |
| Public samples and documentation | Public ACL/runtime and custom-kernel packaging usage | Private RA/HCCP/provider contracts |
| Installed target library and driver | Runtime availability and ABI for the selected target | Completion of an NDS storage request without an NDS-level test |
| Bounded target experiment | The exact tested end-to-end claim | Untested modes, sizes, topologies, or versions |

## Primary sources

### HCOMM

[HCOMM](https://gitcode.com/cann/hcomm) is the primary reference for the
CANN-side HCCP/RA lifecycle and NPU transport boundary. Use the release
matching the selected CANN installation; NDS's current source basis is the
target checkout tagged `v9.0.0`.

NDS derives the following from HCOMM source:

- HCCP/RA rdev, QP, MR, and teardown ordering.
- Offline rdev use of `NOTIFY`, QP peer fields, and CPU-side path-MTU policy.
- Host RA's post-then-runtime-doorbell sequence and the distinction between a
  successful post, a local CQE, and NDS protocol completion.
- AI-QP creation modes and the separation of HCCP internal CQ handling from an
  NDS completion interface.
- The device-side provider-loader pattern used by the standard-CP1 AICPU path.
- The fact that HCOMM collective transport kernels require reciprocal peer
  machinery, flags, and synchronization. They are not an NDS fallback.

HCOMM also fetches a Huawei-patched rdma-core 42.7 source tree. Its top-level
configuration builds `kern-abi` headers, not an HNS provider shared object for
NDS. The patched `providers/hns` source is the most specific open-source
reference for the targeted userspace provider ABI:

- SQ/RQ WQE and SCQ/RCQ CQE byte layouts and bit fields.
- Queue producer and consumer indices, ownership checks, and ordering
  barriers.
- Record-doorbell versus MMIO-doorbell selection and values.
- Custom data-plane exports for queue buffers, indices, record doorbells, and
  doorbell registers.

Relevant paths include `src/platform/hccp`,
`third_party/rdma-core-42.7/providers/hns/hns_roce_u_hw_v2.c`, and
`third_party/rdma-core-42.7/providers/hns/hns_roce_u_ai.c`.

### HCCL

[HCCL](https://gitcode.com/cann/hccl) is a reference for collective-layer
topology, algorithm orchestration, and its use of transport completion and
synchronization. It is not part of NDS's direct NPU-to-CPU storage path.

Use it to understand why a collective transport implementation cannot be
reused piecemeal: it assumes communicator state, rank topology, peer setup,
and matching receive-side progress. Do not use it to infer that an HCCL stream
event, package launch, or collective-internal CQ action completes an NDS
command.

### Upstream RDMA sources

[linux-rdma/rdma-core](https://github.com/linux-rdma/rdma-core) provides the
generic userspace verbs and HNS provider baseline. The Linux
[HNS RoCE driver](https://github.com/torvalds/linux/tree/master/drivers/infiniband/hw/hns)
provides kernel-side context for resource allocation, QP/CQ programming, and
doorbell support. These sources are useful comparisons when separating generic
HNS mechanisms from Huawei's Ascend extensions.

They are not a substitute for the patched HCOMM source or the target driver:
NDS must not assume upstream versions support Ascend-specific RA, AI-QP,
provider, or mapping extensions.

### Public Ascend material

[Ascend samples](https://github.com/Ascend/samples) and
[Ascend documentation](https://github.com/Ascend/docs) are useful only for
public ACL/runtime use and standard custom-operator/AICPU package structure.
They do not publish the private HCCP/RA or device-side RNIC mapping contract.

## Rules for NDS changes

1. Cite the source and exact version in a design note when an NDS decision
   depends on an external implementation detail.
2. Keep copied knowledge as a small NDS-owned ABI declaration or explanation;
   never copy provider, driver, HCOMM, or HCCL implementation code.
3. Keep provider loading, library names, symbol checks, and ABI validation in
   NDS loader modules. Missing required symbols fail closed.
4. Do not exchange provider objects, queue addresses, doorbell addresses, or
   HCCP handles with the CPU endpoint. NDS exchanges only its versioned wire
   records.
5. When source and installed behavior differ, the installed selected CANN and
   driver win. Record the bounded target experiment before changing another
   variable.

## Related NDS documents

- [HCCP QP and MR lifecycle](hccp-resources.md)
- [Linkage and runtime ABI](linkage.md)
- [NPU backends](modes.md)
- [AIV](aiv.md) and [AICPU](aicpu.md)
