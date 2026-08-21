# NDS Benchmarks

This directory contains target-only performance workloads. It is excluded
from normal builds and CI; configure it with `-DNDS_BUILD_BENCHMARKS=ON`.

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

`nds_cpu_hbm_bench_server` and `nds_cpu_hbm_bench_peer` reverse the direction:
the CPU server owns the timed loop and issues RDMA Read or Write directly to
an NPU HBM registration published by the passive peer. The CPU path submits a
single linked WQE window with only the final WR signaled and polls that final
CQE. `--in-flight` controls the window and the server's CPU QP send depth.
These results measure CPU-RNIC submission and completion, not AICPU operator
execution.

`nds_aicpu_verbs_bench_client` and `nds_aiv_verbs_bench_client` measure the
same RDMA Read/Write workload through the AICPU and AIV device kernel paths.
Each WQE post is a device kernel launch, and completions are polled through
a separate device PollCq kernel. The benchmarks require the caller-owned CQ
flag (`NDS_RA_AI_CALLER_POLLS_CQ`). Every WQE is signaled; there is no
deferred-doorbell or group-signaling control because the AICPU provider's
`ibv_exp_post_send` and the AIV `PostSend` have no deferred-doorbell ABI.
`--wr-per-doorbell` and `--ring-last` are not available on these paths.
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

`--wr-per-doorbell` controls an experimental Write doorbell group; `--ring-last`
uses the whole window. `--max-wr-bytes` splits one logical transfer into WQEs
no larger than the specified size, and `--max-wrs-per-window` limits actual
submitted WQEs. They default to 64 KiB and 64 respectively. On the current
target, 256 KiB Write WQEs are slow, while four 64 KiB WQEs with at most 64
submitted WQEs per window completed at about 6.3 GiB/s. Set
`--max-wr-bytes 0` to test an unsplit WQE. These are target observations, not a
general RA contract.

Run the server on the CPU endpoint first, binding `--listen` to its RoCE IP.
Then run the client with the same `--server` address, matching `--operation`
`--bytes`, and `--in-flight`. Both commands are intentionally target-only and
must be wrapped in a bounded timeout. The JSON line is the measured result.
