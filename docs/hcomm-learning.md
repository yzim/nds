# HCOMM and Ascend Communication Learning Notes

This document records observations made while studying HCOMM, HCCL, the
HCOMM-patched HNS provider, and installed CANN operators. It is a learning
notebook, not an NDS architecture contract and not a list of external source
facts that NDS depends on. NDS source provenance remains in
`open-source-references.md`.

## Terminology and ownership

- A QP is the primitive that accepts work requests and owns SQ/RQ state.
  Low-level `post_send` and `post_recv` operations take a QP, not a connection.
- A transport or channel can own one or more QPs, MRs, peer metadata, and
  synchronization state.
- HCOMM uses RMA as an umbrella for remote-memory operations across transports.
  RoCE uses QPs/CQs; UB uses different native resources such as Jetties/JFCs.
- Observing UB-related HCOMM code does not establish UB support on a particular
  installed target.

The host CPU creates and connects communication resources. Device execution
uses a device-visible subset of those resources; it does not recreate the
control plane.

## RA

RA is more than a set of names equivalent to `libibverbs`. RA/HCCP keeps
state behind its QP handles and exposes lifecycle, post, doorbell, and CQ
operations. The host handle is appropriate for host calls but is not the
object an AIV operator should dereference.

For AI QPs, HCCP can provide device dataplane information containing queue
buffers, producer/consumer locations, doorbell information, and CQ metadata.
That device-safe information is more useful to AIV than a host QP handle.

## HNS provider findings

HCOMM fetches and patches rdma-core 42.7 as part of its own build inputs. The
patched HNS provider is valuable for learning expected queue layouts and
provider behavior. It indicates that `aiQpAddr` refers to a device-visible
provider QP object compatible with `struct ibv_qp`, while the containing HNS
object holds private SQ/RQ, queue-buffer, doorbell, and producer state.

The patched provider contains ordinary HNS implementations of:

- send posting;
- receive posting;
- CQ polling;
- SQ/RQ and SCQ/RCQ dataplane metadata extraction;
- record and MMIO doorbell handling.

These implementations establish feasibility and explain layouts. They do not
automatically create a supported AICPU operator API.

The receive-doorbell branch is driven by a capability flag returned during QP
creation, not by each `ibv_post_recv` call. The provider roughly does this:

```text
allocate RQ doorbell record (rdb)
  -> pass rdb address in the QP-create command
  -> kernel driver returns cap_flags
  -> provider stores qp->flags = cap_flags
  -> post_recv selects record DB or MMIO DB
```

With `HNS_ROCE_QP_CAP_RQ_RECORD_DB`, `post_recv` writes the new RQ producer
index to `rdb`; otherwise it calls the provider's MMIO doorbell update helper.
The kernel/device capability and QP setup choose this mode. It is not an
`ibv_post_recv` argument.

## Dataplane-info consumer

`roce_get_qp_data_plane_info()` is not normally called by an AIV or AICPU
operator. In HCCP it is called while preparing the custom AI-QP creation
response, when AI operation support is enabled. HCCP copies the provider's
SQ and RQ fields into its `AiDataPlaneInfo` response and returns them through
the RA/HCCP boundary.

The resulting path is:

```text
HCCP QP creation
  -> libhns-rdmav25.so:roce_get_qp_data_plane_info()
  -> HCCP AiDataPlaneInfo response (SQ + RQ)
  -> HCOMM host transport
  -> device-safe queue descriptor for an AIV/direct dataplane consumer
```

The returned RQ member contains the RQ buffer, depth, WQE size, producer and
consumer addresses, software-doorbell address, and hardware-doorbell address.
The standard-CP1 provider-post path generally uses the compact QP pointer and
provider extension instead; it does not need to call this export itself.

## Doorbell formats

The patched HNS RoCE v2 provider uses a 64-bit MMIO doorbell record with these
fields:

```text
bits  0..23  DB_TAG       QPN for SQ/RQ, CQN for CQ
bits 24..27  DB_CMD       doorbell command
bits 32..47  DB_PI        SQ/RQ producer index
bits 48..50  DB_SL        SQ service level
bits 32..55  DB_CQ_CI     CQ consumer index (CQ commands)
bit      56  DB_CQ_NOTIFY notification selector (CQ arm)
bits 57..58  DB_CQ_CMD_SN CQ arm sequence (CQ arm)
```

The command identifies the target operation:

```text
SQ  -> HNS_ROCE_V2_SQ_DB, DB_TAG=QPN, DB_PI=sq.head, DB_SL=qp->sl
RQ  -> HNS_ROCE_V2_RQ_DB, DB_TAG=QPN, DB_PI=rq.head
CQ  -> HNS_ROCE_V2_CQ_DB_PTR, DB_TAG=CQN, DB_CQ_CI=cq.cons_index
CQ arm -> HNS_ROCE_V2_CQ_DB_NTR, plus arm sequence and notify fields
```

Record/software doorbells use queue-specific memory addresses, so the value
does not need a QPN/CQN tag:

```text
SQ record:  low 16 bits of sq.head
RQ record:  low 16 bits of rq.head
CQ record:  low 24 bits of cq.cons_index
```

The `db_info` returned by HNS `ibv_exp_post_send` in provider-OP mode is a
HCOMM dispatcher payload containing the SQ service level, producer index, and
QPN. It is not by itself a portable raw-MMIO ABI; the dispatcher combines it
with `dbIndex`/doorbell mapping to perform the final doorbell action.

HCOMM's inspected standard-CP1 provider wrapper dynamically resolves only:

```text
libhns-rdmav25.so:ibv_exp_post_send
libhns-rdmav25.so:ibv_ext_post_send
```

No corresponding HCOMM AICPU wrapper for `post_recv` or `poll_cq` was found in
that path. Host HCCP and `rdma_lite` do have receive-post and CQ-poll
operations, but those are separate interfaces and execution environments.

Additional matching source can be materialized remotely on the target host for
research. It should remain outside the NDS build. Useful next references are
the HCOMM-patched HNS provider, matching HNS kernel driver, HCCP sources, and
URMA sources. Source observations must still be checked against installed
CANN libraries and bounded target experiments.

## HCOMM's NPU-side internal surface

HCOMM does not present its internal NPU transport as a public device-side
verbs matrix.

The inspected standard-CP1 AICPU path is approximately:

```text
HcclAicpuUtils::PostSend
  -> Transport::HcclBatchRead or HcclBatchWrite
  -> TransportDeviceIbverbs::HnsPostSend
  -> provider ibv_exp_post_send
  -> AICPU dispatcher/doorbell task
```

The compact `HcclQpInfoV2` carries `qpPtr`, `sqIndex`, and `dbIndex`. HCOMM
also has `HcclAiRMAQueueInfo`, containing SQ, RQ, send-CQ, and receive-CQ
dataplane descriptors. The latter is closer to what direct AIV queue code
needs.

Higher HCOMM layers expose semantic operations such as transport Read/Write
and batch Read/Write. They are not evidence of a stable public `post_send`,
`post_recv`, and `poll_cq` API for arbitrary installed operators.

## Torch eager execution

For an ordinary Python call such as:

```python
torch.distributed.all_reduce(x)
```

the common eager path is host-facing:

```text
Python/Torch-NPU
  -> host ProcessGroupHCCL or HCCL runtime API
  -> host resource preparation and device-task submission
  -> installed device communication implementation
```

The Python call is not translated into an AIV function. Host HCCL logic can
enqueue asynchronous device work while remaining host code.

## Graph execution

A graph compiler does not extract arbitrary host HCCL C++ and convert it to
AIV or AICPU machine code. Communication graph support is explicitly supplied
by the framework/runtime:

1. A registered graph operator, such as an HCCL collective node.
2. Host-side lowering, resource preparation, tiling, and launch metadata.
3. A prebuilt device implementation or runtime communication task.

A graph can therefore contain:

```text
MatMul -> HcomAllReduce -> Relu
```

while host HCCL still prepares communication resources and submits the device
tasks associated with the `HcomAllReduce` node.

## AscendC fused operators

Installed fused AscendC operators use a different but explicit device-client
model. The installed CANN interface contains code shaped like:

```cpp
AscendC::Hccl<AscendC::HCCL_SERVER_TYPE_AICPU> hccl;
hccl.Init(AscendC::GetHcclContext<0>());
auto handle = hccl.AllGather<true>(...);
hccl.Wait(handle);
```

This `AscendC::Hccl` code is compiled into the AIV/AIC operator. It is a
task/message client, not direct verbs. Its operations include collectives and
operations such as `BatchWrite`, plus `Commit`, `Query`, `Wait`, and
`Finalize`.

`GetHcclContext()` returns device memory prepared by the host/runtime. The
device client places commands and observes progress through that context. With
`HCCL_SERVER_TYPE_AICPU`, the AICPU communication server consumes the commands
and performs the larger HCOMM/HCCL transport workflow.

The placement is:

```text
Host CPU: QP/MR resources, communication context, tiling, graph/task launch
AIV/AIC:  compiled AscendC operator and HCCL task client
AICPU:    communication server, algorithms, and transport execution
```

The host implementation is not compiled into the graph. Only the explicitly
written AscendC device client is compiled into the fused operator.

## Questions still open

- Which installed CANN interfaces, if any, intentionally support direct AIV
  SQ/RQ/CQ manipulation outside HCCL fused operators?
- Is there a supported standard-CP1 receive-post or CQ-poll ABI, distinct from
  provider internals and host `rdma_lite`?
- Which completion modes are exposed to installed operators rather than kept
  inside HCCP/HCCL?
- What operator-registration and context-injection mechanism is available to
  a third-party storage operator on the selected CANN release?
