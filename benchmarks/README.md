# NDS Benchmarks

This directory contains target-only performance workloads. It is excluded
from normal builds and CI; configure it with `-DNDS_BUILD_BENCHMARKS=ON`.

See [the benchmark report](../docs/benchmark.md) for the durable methodology,
matched configurations, current target results, and known limitations. This
file remains the directory-local command and workload guide.

Benchmarks must separate session/bootstrap cost from steady-state operation
cost, use bounded warmups and iterations, and record the backend, payload
size, host topology, CANN version, and commit under ignored `.local/` state.

Storage benchmarks should reuse one persistent NDS session. Reconnecting for
each operation measures setup cost rather than storage performance.

`nds_ra_verbs_bench_client` and `nds_ra_verbs_bench_server` measure direct
verbs-layer RDMA Read or Write between NPU memory and a CPU DRAM registration.
They post bounded windows and drain the final signaled completion boundary
before reusing the matching NPU-memory and CPU-DRAM slots. `--ring-last` is an
experimental path: a 2-WR write passed on the current target, but a matching
read returned a provider CQ error, so it is kept out of the measured path until
the V2 batching semantics are understood.
`--in-flight` defaults to 64 and is limited to 1024. Their JSON result excludes
runtime, QP, and MR setup and includes RA post plus completion observation.
CANN RA's send ABI does not preserve an application WR ID, so the RA benchmark
validates completion status.

`nds_cpu_multi_verbs_bench_server` is a passive standard-`libibverbs` CPU
server for `nds_npu_peer_verbs_bench` client runs. It uses the same batched QP
and MR wire exchange as the NPU peer benchmark, creates one CPU RC QP and CPU
DRAM MR per requested QP, and does not submit data-plane work. It permits an
NPU client benchmark to compare a CPU-RNIC DRAM target with an NPU-RNIC HBM
target while preserving the client-side QP count and workload geometry.

`nds_cpu_hbm_bench_server` and `nds_cpu_hbm_bench_peer` reverse the direction:
the CPU server owns the timed loop and issues RDMA Read or Write directly to
an NPU HBM registration published by the passive peer. The CPU path submits a
single linked WQE window with only the final WR signaled and polls that final
CQE. `--in-flight` controls the window and the server's CPU QP send depth.
These results measure CPU-RNIC submission and completion, not AICPU operator
execution. `--qps` is a temporary benchmark-only harness: it creates one
independent transport and TCP port per QP (`base_port + index`) and aggregates
concurrent workers. The NPU peer launches one child process per QP because the
current RA lifecycle rejects repeated `RaInit` in one process. It does not
represent the official multi-QP `Transport` API; production multi-QP support
remains a roadmap item with explicit QP selection, CQ ownership, credits, and
resource-lifetime rules.
Pass matching `--mr-bytes` values to both programs to register a larger CPU
and NPU HBM region. With `--random-addresses`, the server selects one unique,
reproducible offset per warmup and measured operation and applies it to both
regions. The MR must contain at least `bytes * (warmup + iterations)` bytes;
address generation and MR registration are outside the timed interval.
`--post-batch` controls the maximum linked list submitted through each CPU
`ibv_post_send`; it defaults to the full completion window. The server signals
only the final WQE of the entire window and polls that one CQE after all lists
are posted. Unlike RA, CPU verbs has no public deferred-doorbell API, so every
`ibv_post_send` is also a submission event.
The CPU verbs backend retains one SGE/WR cache per QP sized to the largest
submitted post list, so repeated random-address windows do not allocate those
host records in their timed path.
Pass `--host-pinned` to `nds_cpu_hbm_bench_peer` to allocate page-locked host
memory with `aclrtHostRegister` and register its returned NPU-visible mapping
through RA. This is an opt-in CPU-to-NPU-RNIC control that excludes NPU HBM
from the remote data target; the allocation, host registration, and RA MR
registration remain outside the timed interval.

`nds_npu_peer_verbs_bench` exercises the transport's multi-QP path between
two NPUs. One `Transport` owns all QPs and one TCP control socket exchanges the
QP and MR records; the measured dataplane is NPU RoCE. Run the server on one
logical device with `--role server --listen ...`, and the client on the other
with `--role client --server ...`, using the same `--qps`, `--operation`, and
payload settings. Each logical request uses a unique offset across warmup and
measurement, so the MR is sized for `bytes * (warmup + iterations)` per QP;
this avoids reusing one address range between windows. `--mr-bytes` can reserve
a larger range, and `--random-addresses` selects distinct reproducible aligned
slots within it. The benchmark uses one worker per QP and aggregates the
one-direction throughput. It is target-only performance exploration and does
not initialize HCOMM or HCCL. On the current RA target, host CQ polling is
serialized per benchmark window because the provider does not tolerate
interleaved caller `PollCq` sequences across QPs. For small WQEs, use
`--wr-per-doorbell` and `--wr-per-signal` independently. Set both to the
window WQE count to prepare all WQEs, ring once, and signal only the final WQE.
`--ring-last` remains a compatibility shorthand for setting both. This is a
temporary benchmark workaround, not the official transport CQ-ownership
design.
Pass `--server-host-pinned` only to the server role to allocate the published
MR as page-locked host memory through `aclrtHostRegister` and register the
returned NPU-visible mapping through the server NPU RNIC. Conversely,
`--client-host-pinned` is client-only and makes that client buffer the
page-locked local source/target for the submitted requests; the server keeps
its normal NPU HBM allocation. Both options work with RA, AICPU, or AIV.
Allocation, host registration, and MR registration are outside the timed
interval.

Pass `--backend aicpu --aicpu-kernel-config <CP1-overlay-config>` to exercise
the same peer transport through AICPU operators. The default
`--aicpu-runner device-loop` launches one `NdsAicpuRdmaBenchmark` operator per
window; posting and CQ polling then run inside AICPU, so the measured path
avoids host launch per WQE. `--aicpu-runner host-operators` remains available
as an intentionally slow diagnostic path that launches one operator per WQE.
The passive peer may use the normal RA backend. AICPU operator registration
requires the standard-CP1 package overlay; a generated JSON file without the
matching installed package is insufficient. For `--random-addresses`, the
benchmark copies one per-QP offset table to device memory before the timed
section; AICPU and AIV device loops then use those distinct aligned slots.
`--independent-random-addresses` is an AICPU Write experiment that gives the
client source and server destination different unique permutations; it is not
the default shared-offset random path.
`--aicpu-qp-mode opbase-ext` is a diagnostic comparison only. It requires
`--aicpu-runner host-operators --operation write`: AICPU returns the provider
doorbell descriptor, the host invokes `rtRDMADBSend`, and AICPU polls. For
sequential one-WQE Writes with matching signal and doorbell groups, the
diagnostic posts the group's individual provider WQEs inside one AICPU
operator and returns only its final descriptor. Other shapes retain the
per-WQE operator diagnostic behavior. Keep `normal` as the benchmark default.

Pass `--backend aiv --aiv-kernel <kernel-object>` to run the AIV-resident
peer loop. For direct-RoCE experiments, `--aiv-qp-mode normal` is the default
because HCOMM explicitly uses NORMAL for that path; `opbase-ext` is available
only as a comparison. The AIV peer loop supports grouped doorbells: it writes
each WQE, defers the doorbell for non-boundary WQEs, and rings/signals the
boundary WQE. Use `--wr-per-doorbell` and `--wr-per-signal` to control those
two group sizes independently.

`nds_aicpu_verbs_bench_client` and `nds_aiv_verbs_bench_client` measure the
same RDMA Read/Write workload through the AICPU and AIV device kernel paths.
Each WQE post is a device kernel launch, and completions are polled through
a separate device PollCq kernel. The benchmarks require the caller-owned CQ
flag (`NDS_RA_AI_CALLER_POLLS_CQ`). These legacy single-operation clients do
not expose grouped-doorbell controls in normal-QP mode. The AICPU client also
accepts the constrained `opbase-ext` batch diagnostic for contiguous one-WQE
Writes with matching signal and doorbell groups; it posts the group directly
on AICPU and sends the final provider descriptor through the host runtime.
That limitation is otherwise the CLI surface, not an AIV device-path
limitation: the NPU-peer AIV benchmark exposes grouped doorbells as described
above. The AICPU provider's `ibv_exp_post_send` has no documented generic
deferred-doorbell ABI.
The AICPU client must run with the standard-CP1 package overlay used by
`tests/e2e/run_with_aicpu_package.sh`; loading only the generated JSON does not
install its kernel shared object into CANN's package namespace. AICPU operator
arguments are launch inputs, so the benchmark observes completion through the
device WC buffer rather than reading post/poll result fields back on the host.

`nds_aicpu_device_bench_client` launches `NdsAicpuRdmaBenchmark` once per
warmup or measured sample. The operator itself runs the post/poll loop on the
AICPU and writes a result record to device memory; the host does not launch an
operator per WQE. On the current target, a device-side window larger than one
WQE returns provider completion status 18/vendor error 16, even when only the
last WR is signaled and its CQE is polled. The 4-WQE and 64-WQE cases both
failed this way, so use
`--in-flight 1 --max-wrs-per-window 1` until that provider behavior is
understood. This remains an AICPU-resident loop and excludes per-WQE host
launch overhead from its measured interval.

`nds_npu_peer_verbs_bench --backend aicpu --aicpu-linked-wrs` is a separate
provider-ABI experiment. It constructs a bounded `next`-linked list of up to
16 HNS send WRs and calls `ibv_exp_post_send` once for that list. This tests
linked provider posting only; it does not provide deferred doorbell control.
The default remains one provider call per WR. Keep linked-list results separate
from RA `rtRDMADBSend` ring results.

`nds_aiv_device_bench_client` is the corresponding AIV-resident loop. It
loads the generated `nds_aiv_kernel.o`, allocates the local buffer and AI-QP
MR on NPU memory, and launches `NdsAivRdmaBenchmark` once per warmup or
measured sample. Its endpoint follows the current NDS AIV default of
`OPBASE_EXT`; this differs from HCOMM's `NORMAL` direct-RoCE selection and is
documented as a compatibility gap in `docs/design.md`. The CPU peer still owns
the remote DRAM registration. Its result excludes per-WQE host launch cost and
reports completion polling performed by the AIV operator.

`--wr-per-doorbell` controls the submission group and `--wr-per-signal` controls
the independent CQ-signaling interval. `--max-wr-bytes` splits one logical
transfer into WQEs no larger than the specified size, and
`--max-wrs-per-window` limits actual submitted WQEs. They default to one WQE
per doorbell/signal and 64 KiB/64 WQEs respectively. On the current
target, 256 KiB Write WQEs are slow, while four 64 KiB WQEs with at most 64
submitted WQEs per window completed at about 6.3 GiB/s. Set
`--max-wr-bytes 0` to test an unsplit WQE. These are target observations, not a
general RA contract.

Run the server on the CPU endpoint first, binding `--listen` to its RoCE IP.
Then run the client with the same `--server` address, matching `--operation`
`--bytes`, and `--in-flight`. Both commands are intentionally target-only and
must be wrapped in a bounded timeout. The JSON line is the measured result.
