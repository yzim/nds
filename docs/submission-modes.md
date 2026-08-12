# NDS RDMA submission modes

NDS provides three ways to submit an RDMA Write from one Ascend NPU RNIC to a
plain CPU `libibverbs` RC QP. They share control-plane exchange, RA rdev
lifecycle, QP connection, NPU memory registration, CPU destination-memory
registration, and teardown. They differ in who constructs the WQE, who rings
the SQ doorbell, and how completion is observed.

| Mode | Submitter | RA QP creation mode | WQE and doorbell owner | Completion evidence |
|---|---|---|---|---|
| `host-ra` | Host CPU | OPBASE (`2`) through `RaTypicalQpCreate` | HCCP builds the WQE; host calls `rtRDMADBSend` | Host polls with `RaPollCq`, then CPU peer verifies data |
| `aiv` | Ascend vector core | OPBASE_EXT (`4`), normalized by HCCP to provider OP (`2`) | NDS AIV code writes the SQ and hardware doorbell directly | HCCP owns the AI CQ; CPU peer verification is the application completion boundary |
| `aicpu` | Standard CP1 AICPU | NORMAL (`0`) through `RaAiQpCreate` | NDS AICPU kernel calls the HNS provider, which writes the normal-QP doorbell | HCCP owns the AI CQ; CPU peer verification is the application completion boundary |

The modes are selected with `nds_npu_qp_client --submission-mode`. The CPU
endpoint is always `nds_verbs_server` using ordinary `libibverbs`; it does not
load CANN, HCCP, HCOMM, or HCCL.

Detailed guides:

- [Host RA submission](submission-host-ra.md)
- [AIV submission](submission-aiv.md)
- [AICPU submission](submission-aicpu.md)

## Common lifecycle

All modes begin with the same direct one-NPU lifecycle:

```text
aclInit -> aclrtSetDevice -> rtOpenNetService(--hdcType=18) -> RaInit
  -> RaRdevInitV2(NETWORK_OFFLINE, NOTIFY)
  -> create local RC QP
  -> exchange QPN, PSN, GID, retry, and destination-MR metadata over TCP
  -> RaTypicalQpModify
  -> allocate and register NPU source memory
  -> mode-specific submission
  -> receive CPU transfer verification
  -> deregister MR and destroy QP/rdev
  -> RaDeinit -> rtCloseNetService -> aclFinalize
```

The NDS TCP control plane is not an HCOMM synchronization protocol. Its final
transaction acknowledgment keeps the QP and MRs alive until the CPU has
observed and verified the remote effect. This is required for AI QPs because
ACL kernel synchronization is not RNIC completion and HCCP owns their CQs.

## Choosing a mode

Use `host-ra` for the smallest diagnostic baseline and explicit local CQE
visibility. It has the least device-side code and is the first mode to use when
validating a new machine or connection.

Use `aiv` when the request must be submitted by an Ascend vector core and the
application is prepared to own HNS WQE and doorbell layout. It is the most
hardware-coupled mode.

Use `aicpu` when submission must occur on AICPU but should remain a generic
one-WR provider operation. It avoids manual WQE encoding, but requires a
standard CP1 customer-kernel installation and the CANN-matched HNS provider.

These are implementation choices, not performance rankings. NDS has validated
correctness and bounded repeated AIV submission; it has not published a
comparative throughput or latency benchmark.

## Current feature boundary

- End-to-end CPU-peer validation currently exercises RDMA Write.
- AIV currently implements Write only.
- The AICPU request ABI represents Write, Read, and Send, and its kernel builds
  all three WR forms. Read and Send still need matching CPU-side memory/receive
  setup and end-to-end validation.
- No mode initializes HCOMM/HCCL, consumes a rank table, or requires a second
  NPU.
