# Host RA submission

## Basic function

Host RA submission is NDS's reference data path. The host process asks HCCP to
prepare one signaled RDMA Write, submits the returned doorbell through the CANN
runtime, and explicitly polls the send CQ. The payload starts in registered NPU
device memory and lands in a CPU `libibverbs` MR.

Select it with:

```text
--submission-mode host-ra
```

It is also the default when `--submission-mode` is omitted.

## Data path

```text
NPU device source buffer
  -> RaRegisterMr
  -> RaTypicalSendWr(RDMA_WRITE, signaled)
       HCCP/provider writes the WQE and returns db_index + db_info
  -> rtRDMADBSend(db_index, db_info)
  -> RNIC sends RC packets
  -> CPU RNIC writes the destination MR
  -> RaPollCq(send)
  -> CPU verifies payload and guards, then acknowledges the transaction
```

`RaTypicalSendWr` does not ring this OPBASE Lite QP's doorbell. The explicit
`rtRDMADBSend` call is therefore part of submission, not completion handling.

## NDS interfaces

The principal implementation surfaces are:

- `NpuRaContext`: AscendCL/runtime/RA initialization and `rtRDMADBSend`.
- `NpuRaQp`: rdev/QP lifecycle, MR registration, `RaTypicalSendWr`, and
  `RaPollCq`.
- `src/nic_client/modes/host_ra`: mode-owned request and doorbell submission.
- `TcpControlPlane`: endpoint, destination-MR, and transfer-status exchange.
- `nds_npu_qp_client`: command-line orchestration.
- `nds_verbs_server`: CPU RC QP and destination-MR owner.

The dynamically resolved runtime boundary uses:

```text
RaRdevInitV2
RaTypicalQpCreate
RaGetQpAttr
RaTypicalQpModify
RaRegisterMr / RaDeregisterMr
RaTypicalSendWr
RaPollCq
RaQpDestroy / RaRdevDeinit
rtRDMADBSend
```

## Build and usage

Build on an aarch64 target with `libibverbs` development files available:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Start the CPU endpoint first:

```sh
build/nds_verbs_server \
  --device <cpu-rdma-device> \
  --gid-index <cpu-gid-index> \
  --listen <cpu-roce-ip> \
  --tcp-port <control-port> \
  --bytes 4096
```

Then run the NPU endpoint:

```sh
build/nds_npu_qp_client \
  --ascendcl <cann-root>/aarch64-linux/lib64/libascendcl.so \
  --runtime <cann-root>/aarch64-linux/lib64/libruntime.so \
  --ra <cann-root>/aarch64-linux/lib64/libra.so \
  --npu-ip <npu-roce-ip> \
  --logical-device 0 \
  --physical-device 0 \
  --cpu-ip <cpu-roce-ip> \
  --tcp-port <control-port> \
  --submission-mode host-ra \
  --execute
```

Use a process-level timeout around hardware tests. The client currently
restricts one invocation to one bounded Write.

## HCOMM and HCCP reference basis

The implementation was derived from behavior and ABI in the CANN-matched
HCOMM source, especially:

- `src/platform/hccp/rdma_agent/client/ra_host.c`: exported RA lifecycle and
  `RaPollCq` dispatch.
- `src/platform/hccp/rdma_agent/hdc/ra_hdc_rdma.c`: host-to-HCCP QP and send
  operations.
- `src/platform/hccp/rdma_service/rs_rdma.c`: service-side QP lifecycle and CQ
  ownership.
- `src/platform/resource/transport/host/transport_ibverbs.cc`: HCOMM host
  transport QP setup and send behavior.
- `src/platform/common/adapter/adapter_rts.cc`: runtime RDMA doorbell adapter.

NDS transcribes only the ABI fields and symbols it uses. It does not link
HCOMM or copy HCCP implementation code.

## Choices and hidden decisions

### QP and rdev mode

The host path creates an RC OPBASE QP with `RaTypicalQpCreate`. The offline
rdev uses `NOTIFY (1)`, not `NO_USE (0)`; this is required by the HCCP offline
HDC lifecycle.

NDS keeps the RA Lite context enabled to match the validated direct lifecycle,
but completion consumption for this path is explicit in `RaPollCq`. Do not add
a second application CQ consumer.

### Completion ownership

This is the only NDS mode that treats an NPU-side CQE as directly visible
application evidence. A successful `RaTypicalSendWr` or `rtRDMADBSend` return
is not completion. NDS polls until it receives the one signaled completion,
checks its status, and still waits for CPU payload verification before freeing
resources.

### Path MTU

The CPU QP uses its local active-port MTU, matching HCCP's
`RsDrvQpStateModifytoRtr` behavior. The NDS endpoint MTU field is diagnostic;
it is not negotiated or used to clamp the CPU QP.

### Why this remains the baseline

This path avoids custom NPU kernels and private HNS WQE layout. It separates
connection or registration failures from AIV/AICPU execution failures and
therefore remains the preferred first validation step.
