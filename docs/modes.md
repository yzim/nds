# Submission Modes

NDS provides three ways to submit an RDMA Write from one Ascend NPU RNIC to a
plain CPU `libibverbs` RC QP. They share the HCCP rdev/QP and MR lifecycle
described in [HCCP QP and MR lifecycle](hccp-resources.md). They differ in who
posts the request and who owns the NPU send-CQ path.

| Mode | Submission / post path | RA QP creation mode | NPU send-CQ consumer |
|---|---|---|---|
| `host-ra` | NPU-side host CPU calls `RaTypicalSendWr`, then `rtRDMADBSend` | OPBASE (`2`) through `RaTypicalQpCreate` | NPU-side host CPU calls `RaPollCq` |
| `aiv` | NDS AIV code writes the SQ and hardware doorbell directly | OPBASE_EXT (`4`), normalized by HCCP to provider OP (`2`) | HCCP internally handles the AI-QP CQ on the NPU; NDS does not call `RaPollCq` |
| `aicpu` | Standard CP1 AICPU calls HNS provider `ibv_exp_post_send`, which rings the normal-QP doorbell | NORMAL (`0`) through `RaAiQpCreate` | HCCP internally handles the AI-QP CQ on the NPU; NDS does not call `RaPollCq` |

The modes are selected with `nds_npu_qp_client --submission-mode`. The CPU
endpoint is always `nds_verbs_server` using ordinary `libibverbs`; it does not
load CANN, HCCP, HCOMM, or HCCL.

The CPU peer's payload and guard verification is not an NPU CQ poll. It is an
internal state of the current integration-test server: it holds resources while
the bounded Write harness checks the result. It is not the project-facing
completion interface. AIV and AICPU currently rely on HCCP's internal AI-QP CQ
handling, and defining how NDS exposes or integrates their completion remains
unfinished work.

Detailed guides:

- [Host RA](host-ra.md)
- [AIV](aiv.md)
- [AICPU](aicpu.md)

## Choosing a mode

Use `host-ra` first. It has the smallest hardware-specific surface and gives
the NPU process an explicit send CQE through `RaPollCq`.

Use `aiv` when the request must be submitted by an Ascend vector core and the
application accepts ownership of the CANN-matched HNS WQE and doorbell layout.

Use `aicpu` when submission must occur on AICPU but should remain a one-WR
provider operation. It avoids manual WQE encoding, but requires a standard CP1
customer-kernel installation and the CANN-matched HNS provider.

These are implementation choices, not performance rankings. NDS has validated
correctness and bounded repeated AIV submission; it has not published a
comparative throughput or latency benchmark.

## Current feature boundary

- End-to-end CPU-peer validation currently exercises RDMA Write.
- The CPU payload/guard acknowledgment exists only for the bounded integration
  test. It is not the final completion interface.
- Host RA exposes an NDS-owned send-CQ poll. AIV and AICPU need a defined
  project-facing completion contract above or alongside HCCP's internal AI-CQ
  handling.
- AIV currently implements Write only.
- The AICPU request ABI represents Write, Read, and Send, and its kernel builds
  all three WR forms. Read and Send still need matching CPU-side memory/receive
  setup and end-to-end validation.
- No mode initializes HCOMM/HCCL, consumes a rank table, or requires a second
  NPU.
