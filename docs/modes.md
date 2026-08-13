# NPU Backends

NDS has three implementations for posting the NPU-to-CPU storage-command
Send. They share the HCCP rdev/QP and MR lifecycle in
[HCCP QP and MR lifecycle](hccp-resources.md). The backend only changes how
the NPU posts the command. CPU storage execution and protocol completion are
the same in every mode.

| Mode | Command `post_send` | QP mode | Protocol completion |
|---|---|---|---|
| `host-ra` | Host calls `RaTypicalSendWr`, then `rtRDMADBSend` | OPBASE | CPU writes NDS completion record; host copies and polls it |
| `aiv` | AIV writes a Send WQE and SQ doorbell | OPBASE_EXT/provider OP | CPU writes NDS completion record; host copies and polls it |
| `aicpu` | CP1 dynamically loads HNS provider and calls `ibv_exp_post_send` | NORMAL | CPU writes NDS completion record; host copies and polls it |

The CPU uses one `libibverbs` CQ. It explicitly polls a command Receive CQE,
posts its storage RDMA operation followed by the terminal completion Write on
the same RC QP, and polls that signaled send CQE. HCCP AI-QP CQ handling is not
an NDS completion interface. A host-RA local CQE is not the storage result.

Use `host-ra` first because it has the smallest hardware-specific surface. Use
`aiv` for direct vector-core WQE/doorbell posting, and `aicpu` for a
provider-owned post from standard CP1. These are implementation choices, not
performance rankings.

NDS has validated one bounded storage Write in all three modes, and a Host RA
Read from a fresh zeroed namespace. It has not published throughput or latency
results.

The initial protocol permits one command in flight on one RC QP. Queueing,
multi-QP sessions, and an NPU Receive-based completion option are tracked in
[the roadmap](roadmap.md).

- [Host RA](host-ra.md)
- [AIV](aiv.md)
- [AICPU](aicpu.md)
