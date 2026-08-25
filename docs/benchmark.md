# Benchmark Report

This report condenses the target experiments recorded under `.local/`. It is
the durable summary of the current NDS performance exploration; the raw logs
remain the evidence record for each run. Unless a section says otherwise,
results were collected on `node200` with CANN 9.0.0 on 2026-08-21 through
2026-08-25.

These are target-specific measurements, not portable performance claims. Every
numeric result in this report completed data verification and endpoint
teardown successfully; failed experiments are described only as limitations.
Setup, QP creation, memory registration, address-table construction, and
bootstrap are outside the timed interval unless explicitly stated. Throughput
uses GiB/s, where 1 GiB/s is 2^30 bytes/s. The corresponding WQE rate is often
the more useful metric for small transfers.

## Testbed

The measurements were run on the approved Linux target `node200` with an
Ascend 910B3 accelerator, CANN 9.0.0, and driver package 25.5.1. The
production-shaped NDS path has one Ascend NPU RNIC and one CPU-side RoCE RNIC:

```text
NPU0 HBM or host-pinned memory
        |
   NPU RoCE RNIC
        |
      RoCE fabric
        |
   CPU RoCE RNIC
        |
CPU DRAM / libibverbs server
```

The CPU endpoint is a normal Linux `libibverbs` process. It does not initialize
HCOMM, HCCL, TSD, or a second NPU in the production-shaped path. The NPU
endpoint uses the NDS RA, AIV, or standard-CP1 AICPU execution backend through
the installed CANN runtime. CPU DRAM is registered by the CPU RNIC; NPU HBM
and page-locked host memory are registered by the NPU RNIC.

### CPU RNIC Baseline

The CPU endpoint uses one port of a RoCE-capable Ethernet RNIC with RC
transports and standard `libibverbs`. The retained target evidence does not
include a stable HCA model or firmware identifier, so those are intentionally
not stated here. The host-side path negotiated PCIe Gen4 x8.

The direct CPU-RNIC reference was a same-RNIC `perftest ib_write_bw` loopback:
512-byte Writes, eight QPs, 4,096 transmit depth, 16-WR post lists, and one
million operations per QP. It measured **91.10 Gb/s**, **10.605 GiB/s**, and
22.241M messages/s. This is the CPU RNIC's standalone request-rate baseline;
the CPU-RNIC-to-NPU path reached 7.039 GiB/s under the matched high-depth NDS
configuration and therefore includes the second RNIC, NPU target, and NDS
submission path.

Hardware bandwidth references:

| Component | Bandwidth |
| --- | ---: |
| CPU-side RNIC | Approximately 100-Gb/s class; 91.10 Gb/s measured loopback, approximately 103 Gb/s in an earlier standalone check |
| Host PCIe link | PCIe Gen4 x8; approximately 126 Gb/s (15.75 GB/s, 14.67 GiB/s) one-way at the 128b/130b link rate, before protocol overhead |

Backend-only comparisons also use the temporary two-NPU RoCE topology:

```text
NPU0 backend under test -- NPU RNIC -- RoCE fabric -- NPU1 RA peer
```

This isolates NPU submission and completion behavior from the CPU server, but
it is not the NDS production topology or a storage-protocol concurrency claim.
The CPU-initiated harness reverses the active side in the production-shaped
topology: the CPU server issues RDMA Read or Write to an NPU0 HBM or
NPU-RNIC-registered host-pinned MR while the NPU peer remains passive.

The two-NPU reference path is approximately 200 Gb/s RoCE. It is a backend
comparison only; the CPU-RNIC bandwidth and PCIe reference are documented in
the CPU RNIC subsection above. NPU-to-CPU runs use one NPU RNIC and one CPU
RNIC, while the temporary multi-QP benchmark varies logical QP count within
that physical testbed.

## How To Compare Results

Only compare runs with the same:

- logical operation and direction;
- WQE payload size and logical request size;
- QP count and in-flight depth per QP;
- SQ/window size and doorbell/signaling policy;
- local and remote memory type;
- address policy, warmups, measured iterations, and completion policy.

The CPU server and NPU client do not submit through the same API. A matched
configuration therefore means matched workload geometry, not identical
submission mechanics. A CPU `ibv_post_send` list is a submission event for
every list. RA can prepare WQEs and ring one runtime doorbell. AICPU provider
linked WRs reduce the number of provider calls, but they do not expose a
general deferred-doorbell ABI.

The benchmark result excludes setup but includes the steady-state submission
and completion-observation path used by that backend. It is not end-to-end NDS
storage latency or bandwidth. Storage completion is still the CPU-written NDS
completion record, not a local CQE, provider poll, runtime synchronization, or
operator launch.

## Backend And Memory Matrix

| Backend | Active submitter | Typical remote target | Submission model |
| --- | --- | --- | --- |
| CPU `libibverbs` | CPU server | NPU HBM or NPU-RNIC host-pinned MR | Linked CPU WR lists; final-WR signaling |
| RA | NPU host thread | NPU peer HBM or host-pinned MR | One RA post per WQE plus optional grouped `rtRDMADBSend` |
| AIV | NPU vector operator | NPU peer HBM | Device SQ writes with optional grouped doorbells |
| AICPU provider | NPU standard-CP1 operator | CPU DRAM or NPU peer MR | HNS provider post per WR or bounded `next` chain |
| AICPU raw SQ | NPU standard-CP1 diagnostic | NPU peer MR | Direct SQ publication and grouped MMIO doorbell |

The tested memory classes are CPU DRAM registered by the CPU RNIC, NPU HBM
registered by the NPU RNIC, and page-locked host memory registered through
`aclrtHostRegister` and then mapped through the NPU RNIC. Host-pinned memory is
not equivalent to CPU DRAM: it is an NPU-RNIC MR backed by a host allocation.

The two-NPU RoCE path is the primary backend-performance reference because it
removes the CPU server's protocol and CPU-RNIC path from the NPU submission
comparison. The one-NPU-to-CPU-RNIC path is the interoperability and storage
shape reference.

## Reference Results

### CPU Server: 512-Byte Writes

The CPU-initiated path is the closest current approximation to a real CPU
storage server. It issues the timed RDMA operation from the CPU and uses
`libibverbs`; the NPU peer is passive.

| Path | QPs | In flight/QP | SQ/window | CPU post list | Result |
| --- | ---: | ---: | ---: | ---: | ---: |
| CPU DRAM -> NPU HBM | 8 | 4,096 | 4,096 | 16 WRs | **7.039 GiB/s**, 14.762M WQEs/s |
| CPU DRAM -> NPU HBM | 8 | 4,096 | 4,096 | 16 WRs, prior sample | 7.091 GiB/s, 14.871M WQEs/s |
| CPU DRAM -> NPU host-pinned MR | 8 | 4,096 | 4,096 | 16 WRs | 6.899 GiB/s, 14.468M WQEs/s |
| CPU-RNIC loopback control | 8 | 4,096 | 4,096 | 16 WRs | 10.605 GiB/s, 22.241M WQEs/s |

Common settings were 512-byte Writes, 1 GiB MR per QP, cyclic-contiguous
addresses for the fresh reproduction, two warmups, 1,000,000 measured
operations per QP, and final-only signaling with one final CQE polled per
window. The CPU WR cache was enabled for the 7.039 GiB/s reproduction. The
7.039 and 7.091 GiB/s samples are consistent single-run variation.

The host-pinned target is 2.7% below the HBM target in the paired sample. The
CPU-RNIC loopback is substantially faster, so the large gap is not explained
by the NPU HBM target or random address generation alone. The remaining limit
is in the two-RNIC path and/or CPU-side NDS/libibverbs submission behavior.

With the smaller, geometry-matched common configuration of eight QPs, 1,024
in-flight WQEs/QP, 1,024-WQE windows, 1 GiB MR/QP, and 16-WR CPU post lists,
CPU DRAM -> NPU HBM reached **3.567 GiB/s**. This is the valid comparison point
against the NPU-active AICPU result below; the 4,096-depth CPU result is not a
matched comparison.

The one-QP CPU control also measured both operation types at the same
0.449 GiB/s:

| Operation | QPs | In flight/QP | SQ/window | Result |
| --- | ---: | ---: | ---: | ---: |
| CPU-initiated Read | 1 | 1,024 | 1,024 | 0.449 GiB/s, 0.941M WQEs/s |
| CPU-initiated Write | 1 | 1,024 | 1,024 | 0.449 GiB/s, 0.941M WQEs/s |

These one-QP results are request-rate controls, not saturation results.

Evidence: `.local/logs/2026-08-25-cpu-hbm-512b-contiguous-repro-node200.txt`,
`.local/logs/2026-08-24-cpu-hbm-512b-contiguous-wr-cache-qps8-node200.txt`,
`.local/logs/2026-08-24-cpu-host-pinned-512b-qps8-node200.txt`,
`.local/logs/2026-08-24-cpu-rnic-loopback-512b-qps8-node200.txt`,
`.local/logs/2026-08-25-cpu-dram-hbm-matched-common-1024-node200.txt`, and
`.local/logs/2026-08-24-cpu-hbm-512b-node200.txt`.

### NPU HBM Client: CPU DRAM Target

This is the original NDS interoperability shape: an AICPU client uses NPU HBM
and a standard `libibverbs` CPU server owns registered CPU DRAM.

| Workload | QPs | In flight | Window | Result |
| --- | ---: | ---: | ---: | ---: |
| 64 KiB Write | 1 | 32 | 64 WQEs | **9.468 GiB/s** |
| 512 B Write | 1 | 32 | 64 WQEs | 0.568 GiB/s |
| 512 B Write, tuned | 1 | 512 | 512 WQEs | **0.692 GiB/s** |

The 64 KiB run used two warmups and 100,000 measured operations. The tuned
512-byte run used the same warmup and iteration count. The original AICPU path
uses individual provider posts, final-WQE signaling, and `poll_batch=1`; its
window setting controls how many WQEs the device loop handles before waiting,
not an RA-style deferred doorbell.

At 512 B, a single-QP depth sweep rose from 0.098 GiB/s at depth 1 to
0.691 GiB/s at depth 512, then fell slightly to 0.673 GiB/s at depth 1,024.
At fixed depth 512, changing the window from 64 to 512 WQEs produced
0.637, 0.646, 0.682, and 0.667 GiB/s. Outstanding depth is the meaningful
factor in this original path; poll batch and window changes are secondary.

The largest common supported comparison with the CPU server used 512-byte
Writes, eight QPs, 1,024 in-flight WQEs/QP, 1,024-WQE windows, 1 GiB MR/QP,
and 1,000,000 operations/QP:

| Direction | Submission | Result |
| --- | --- | ---: |
| CPU DRAM -> NPU HBM | CPU 16-WR lists, final-WR completion | **3.567 GiB/s** |
| NPU HBM -> CPU DRAM | AICPU linked 16-WR posts, final-WQE completion | **3.180 GiB/s** |

Both directions passed. The 3.180 GiB/s result is the meaningful direction
comparison; comparing it with the 7.039 GiB/s CPU result would mix window and
in-flight depth.

Evidence: `.local/logs/2026-08-25-aicpu-hbm-client-cpu-dram-fixed-node200.txt`,
`.local/logs/2026-08-25-aicpu-hbm-client-cpu-dram-512-fixed-node200.txt`,
`.local/logs/2026-08-25-aicpu-hbm-client-cpu-dram-512-reverse-fixed-node200.txt`,
`.local/logs/2026-08-25-aicpu-hbm-client-cpu-dram-512-depth-sweep-fixed-node200.txt`,
`.local/logs/2026-08-25-aicpu-hbm-client-cpu-dram-512-window-sweep-fixed-node200.txt`,
and `.local/logs/2026-08-25-cpu-dram-hbm-matched-common-1024-node200.txt`.

### Two-NPU RoCE Backend Reference

These runs use NPU0 -> NPU1 Writes and are not CPU-server measurements.

| Backend and workload | QPs | In flight/QP | Submission policy | Result |
| --- | ---: | ---: | --- | ---: |
| AICPU, 4 MiB logical / 64 KiB WQEs | 8 | 16 | linked 16-WR chains, final only | **22.805 GiB/s** |
| AICPU, same workload control | 8 | 16 | individual provider WRs, final only | 22.792 GiB/s |
| AIV, 4 MiB logical / 64 KiB WQEs | 8 | 8 | 64-WQE doorbell/signal groups | 22.478 GiB/s |
| RA, 4 MiB logical / 64 KiB WQEs | 8 recovery | 8 | 128-WQE doorbell/signal groups | 19.583 GiB/s |

The AICPU and AIV results are close to the two-NPU 200-Gb/s link reference.
The AICPU linked list is accepted, but it adds no measurable throughput at
the saturated point: resident execution, outstanding depth, and QP
concurrency matter more than `next` links. AIV's 128-WQE group was slower than
its 64-WQE group at the eight-QP point (22.345 versus 22.478 GiB/s).

For 512-byte WQEs, the best standard AICPU provider result was **9.716 GiB/s**
(20.376M WQEs/s): eight QPs, 4,096-WQE SQ/window, shared random unique
source/destination offsets, linked 16-WR posts, and final-only signaling. A
separate 2x2 control at the same geometry measured 9.694 GiB/s with poll batch
one and 8.995 GiB/s with poll batch 16. Individual WRs measured 0.585 and
0.535 GiB/s under those two poll settings. The provider post model, rather
than CQ polling, is the dominant small-WQE cost.

AIV's best comparable random 512-byte result was **2.560 GiB/s** at 16 QPs,
512-WQE windows, and 64-WQE doorbell/signal groups. Its eight-QP result was
2.512 GiB/s. AIV is therefore close to AICPU at 4 KiB (19.697 versus
21.281 GiB/s in the matched random-address comparison) but remains far behind
at 512 B because its WQE rate is low.

RA requires explicit doorbell grouping. In a one-QP 64-KiB-WQE sweep, changing
the ring interval from one to 128 WQEs improved throughput from 6.705 to
16.469 GiB/s; signaling improvements largely plateaued around 128 WQEs.
Higher-QP combinations can fail in provider CQ handling, so the successful
recovery result is not a stable multi-QP saturation claim.

HCCL controls are API-overhead references, not NDS backend baselines. The
tested `HcclBatchSendRecv` path uses receiver-side read-mode operations rather
than one write doorbell for all items. The ordinary HCCL Broadcast small-write
path also submits one transport task per `DataSlice`; its batch mode groups the
runtime task launch, not the underlying data WQEs. A separate HCOMM
`BatchTransfer`/vector one-sided-write path exists in source but was not
exercised by these tests.

| HCCL control | Work unit | Result |
| --- | --- | ---: |
| Public `HcclBatchSendRecv` | 4 KiB item | 0.48235 GB/s |
| Public `HcclBatchSendRecv` | 512 B item | 0.06537 GB/s |
| Two-rank Broadcast small task | 4 KiB / 512 B task | 0.323 / 0.0093 GB/s |
| Forced-RoCE Broadcast | 64 MiB-1 GiB message | about 23.1-23.5 GB/s |

These controls show fixed HCCL task/notification overhead for small units and
must not be interpreted as evidence of RDMA doorbell coalescing.

Evidence: `.local/logs/2026-08-23-hccl-public-batch-sendrecv-node200.txt`,
`.local/logs/2026-08-23-hccl-public-batch-sendrecv-hccs-node200.txt`,
`.local/logs/2026-08-23-hccl-roce-small-write-tasks-node200.txt`, and
`.local/logs/2026-08-23-hccl-roce-oneway-broadcast-node200.txt`.

The AICPU `OPBASE_EXT` probe is also correctness-only: changing the QP mode
alone failed, while the host-assisted descriptor-copy and `rtRDMADBSend`
sequence passed at 0.352 GiB/s for a one-WQE 64 KiB Write. The extra host and
operator synchronization makes that result unsuitable for backend
performance comparison; the resident direct-provider benchmark remains on
`NORMAL`.

Evidence: `.local/logs/2026-08-23-aicpu-opbase-ext-node200.txt` and
`.local/logs/2026-08-23-aicpu-opbase-ext-host-ring-node200.txt`.

Evidence: `.local/logs/2026-08-23-aicpu-linked-qps8-node200.txt`,
`.local/logs/2026-08-23-aicpu-individual-qps8-node200.txt`,
`.local/logs/2026-08-23-aiv-peak-reproduction-node200.txt`,
`.local/logs/2026-08-23-aiv-small-wqe-random-node200.txt`,
`.local/logs/2026-08-23-ra-small-factor-sweep-node200.txt`,
`.local/logs/2026-08-23-aicpu-small-wqe-random-addresses-node200.txt`, and
`.local/logs/2026-08-23-aicpu-small-wqe-batch-factorial-node200.txt`.

## Host-Pinned Memory

### RA

RA supports page-locked host buffers on either or both NPU endpoints in the
tested deferred-doorbell path. These are 4 MiB logical Writes split into
64 KiB WQEs, one QP, eight in flight, 512-WQE SQ/window, random unique
addresses, and a 64-WQE prepared group:

| Local / remote MR | Operation | Result |
| --- | --- | ---: |
| host-pinned / NPU HBM | Write | **18.199 GiB/s** |
| host-pinned / NPU HBM | Read into host-pinned local MR | **14.424 GiB/s** |
| host-pinned / host-pinned | Write | **14.605 GiB/s** |

RA prepares each WQE with `RaSendWrV2` and rings one runtime doorbell for the
group. It does not use provider WR `next` links.

### AICPU Provider And Raw SQ Diagnostics

The standard AICPU provider path has an endpoint-dependent linked-submission
limitation with host-pinned MRs. A one-WQE smoke passes, but linked posting
fails before the first operation for both host-pinned local and remote MRs,
even with one QP and a small MR. Individual posts complete only around
0.546-0.581 GiB/s in the tested eight-QP 512-byte controls. CQ polling in
groups of 16 did not improve the individual-post result (0.576 GiB/s in one
paired run).

This is a provider submission failure, not evidence that host-pinned memory
cannot work. A direct diagnostic that bypasses `ibv_exp_post_send` and writes
the NPU SQ with one MMIO doorbell per 64 WQEs reached **11.003 GiB/s** for
random 512-byte Reads into a host-pinned client MR. A paired provider
comparison measured 0.569 GiB/s for individual posts, 7.822 GiB/s for linked
16-WR posts, and 12.704 GiB/s for linked 64- or 128-WR posts. These raw-SQ
and extended linked-chain results are experimental diagnostics, not the
production AICPU provider path or a public NDS submission contract.

### AIV

A one-QP, one-64-KiB-WQE AIV Write with a host-pinned local client MR passed at
0.984 GiB/s. The eight-QP grouped-doorbell 512-byte host-pinned workload
failed destination verification, including sequential and random variants.
Treat grouped AIV host-pinned operation as unresolved; the one-WQE smoke is
not a bandwidth result.

Evidence: `.local/logs/2026-08-24-ra-npu-peer-client-host-pinned-batch-node200.txt`,
`.local/logs/2026-08-24-ra-npu-peer-client-host-pinned-batch-read-node200.txt`,
`.local/logs/2026-08-24-ra-npu-peer-both-host-pinned-batch-write-node200.txt`,
`.local/logs/2026-08-24-fixed-aiv-raw-aicpu-host-pinned-random-512-compare-node200.txt`,
`.local/logs/2026-08-24-aicpu-raw-sq-npu-peer-client-host-pinned-random-512-read-qp8-db64-node200.txt`,
`.local/logs/2026-08-24-aicpu-npu-peer-host-pinned-qp1-large-peak-failure-node200.txt`,
`.local/logs/2026-08-24-aicpu-npu-peer-host-pinned-qp1-small-individual-node200.txt`,
`.local/logs/2026-08-24-aicpu-npu-peer-host-pinned-qp1-small-linked1-failure-node200.txt`,
`.local/logs/2026-08-24-aicpu-npu-peer-host-pinned-qp8-large-individual-peak-node200.txt`,
`.local/logs/2026-08-24-aicpu-npu-peer-host-pinned-qp8-large-single-node200.txt`, and
`.local/logs/2026-08-24-aicpu-npu-peer-host-pinned-qp8-small-linked-failure-node200.txt`.

## Batch, Ring, And Poll Semantics

The terms describe different layers and must not be conflated:

| Term | Meaning in the current benchmarks |
| --- | --- |
| In flight | Outstanding WQEs per QP before the benchmark reuses request slots |
| Window / SQ depth | WQEs posted before the final completion boundary; provider SQ depths are power-of-two constrained |
| CPU post batch | Maximum WRs in one CPU `ibv_post_send` list; every call still submits to the RNIC |
| RA ring group | Prepared WQEs covered by one `rtRDMADBSend` runtime doorbell |
| AIV doorbell group | Device SQ WQEs published before the boundary WQE rings the SQ |
| AICPU linked chain | Provider `next`-linked WRs in one `ibv_exp_post_send` call; not a deferred doorbell |
| Poll batch | CQEs requested by one provider poll call; it does not batch submission |
| Signal interval | WQEs that share one signaled completion boundary |

The strongest 512-byte CPU configuration used eight QPs, depth/window 4,096,
16-WR CPU post lists, final-only signaling, and one final CQE per window. The
strongest standard AICPU configuration used eight QPs, a 4,096-WQE window,
linked 16-WR posts, final-only signaling, and shared random offsets. For the
original one-QP AICPU-to-CPU-server path, the practical tuned point was depth
512, a 512-WQE window, and `poll_batch=1`.

CQ polling did not explain the AICPU peak: at the random 512-byte workload,
linked 16-WR posts measured 9.694 GiB/s with poll batch one and 8.995 GiB/s
with poll batch 16, while individual posts measured 0.585 and 0.535 GiB/s.
At the 64-KiB saturated workload, poll batch one and 16 were both about
22.8 GiB/s when only the final WQE was signaled. The decisive variable is the
provider submission path and the amount of resident outstanding work.

## ABI And Reproducibility Notes

The original AICPU benchmark first failed because a packed control word with a
zero poll-batch field was rejected. Zero now means the default poll batch of
one, while the `NdsDeviceRdmaBenchmarkArgs` launch record remains 320 bytes.
This is an ABI compatibility fix, not a performance optimization.

The standard-CP1 AICPU package overlay must match the benchmark binary. A
stale or generated-only overlay can fail package registration before any
throughput sample. Record the target, CANN release, package overlay, commit,
backend, memory mode, direction, payload, QPs, depth/window, ring/signal
groups, poll batch, warmups, iterations, and verification status with every
new result.

The temporary benchmark multi-QP harness is not the production multi-QP
transport. It uses one worker or child process per QP and has target-specific
CQ and lifecycle constraints. Do not turn a successful benchmark sweep into a
storage protocol concurrency claim.

## Limits And Next Work

- AICPU provider linked chains above 16 WRs were not generally stable in the
  standard path; a 32-WR test failed at provider post time. The host-pinned
  diagnostic's 64- and 128-WR results are separate experiments.
- AICPU device-loop windows against the CPU DRAM server have failed in some
  builds with completion status 18/vendor error 16, while the same class of
  two-NPU runs passed. Preserve the exact overlay and build when reproducing.
- RA multi-QP small-unit runs can fail in provider CQ handling. A failed CQ
  handshake is not a bandwidth result.
- AIV grouped host-pinned verification is unresolved, and AICPU linked
  host-pinned submission fails at the provider boundary. Do not attribute
  either failure to host-pinned memory without isolating the submission mode.
- Public HCCL batch APIs are not an equivalent NDS small-write primitive.
  `HcclBatchSendRecv` exercised receiver-side read-mode work and measured
  0.48235 GB/s for 4 KiB items and 0.06537 GB/s for 512-byte items; the
  ordinary HCCL small-write task path measured about 0.323 GB/s for 4 KiB and
  0.0093 GB/s for 512 B. These are API-overhead controls, not NDS backend
  baselines.

The next performance work should keep the CPU server as the primary reference,
repeat the matched 512-byte CPU and AICPU direction pair, and separate
two-RNIC transport cost from CPU submission cost with repeated samples and
variance. Any new deferred-submission mechanism must preserve the documented
backend-specific ABI boundary and be validated for correctness before being
treated as a production path.
