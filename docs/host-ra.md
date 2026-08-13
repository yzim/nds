# Host RA Backend

Host RA is the NPU-host-CPU backend for the NDS storage command Send. It is
the baseline because it uses the public CANN RA and runtime boundary without a
device kernel.

## Data Path

```text
NPU host: RaTypicalSendWr(SEND) -> rtRDMADBSend
CPU:      Receive command -> RDMA Read or Write application data
CPU:      RDMA Write terminal completion record -> NPU completion memory
NPU host: aclrtMemcpy(device-to-host) polls terminal completion record
```

The host creates an OPBASE RC QP with `RaTypicalQpCreate`, connects it to the
CPU verbs QP with `RaTypicalQpModify`, and registers the NPU application,
command, and completion allocations with `RaRegisterMr`. The CPU uses the
command descriptor to choose its data operation: storage Write is CPU RDMA
Read; storage Read is CPU RDMA Write.

`RaTypicalSendWr` returns doorbell metadata for this QP, so
`rtRDMADBSend` is required to submit the Send. NDS does not use a Host RA CQE
as protocol completion. The authoritative result is the terminal completion
record written by the CPU after its ordered data operation.

## Interfaces

- `src/npu_client/backend/host_ra/backend.cc`: RA post and runtime doorbell.
- `src/npu_client/backend/support/core/npu_ra_qp.cc`: QP and MR RA calls.
- `src/npu_client/backend/support/core/npu_ra_context.cc`: runtime lifecycle, doorbell, and
  device-to-host completion copy.
- `src/npu_client/transport/connection.cc`: registered buffers and command Send.
- `src/cpu_server/protocol/storage.cc`: CPU Receive, data RDMA, and completion
  Write sequencing.
- `src/cpu_server/backend/verbs/backend.cc`: verbs work requests and CQ polling.

## Usage

Start the CPU server:

```sh
build/nds_verbs_server --device <cpu-rdma-device> --gid-index <gid-index> \
  --listen <cpu-roce-ip> --tcp-port <port> --namespace-bytes 1048576
```

Then submit one command from the NPU:

```sh
build/nds_npu_qp_client --backend host-ra \
  --ascendcl <cann-root>/aarch64-linux/lib64/libascendcl.so \
  --runtime <cann-root>/aarch64-linux/lib64/libruntime.so \
  --ra <cann-root>/aarch64-linux/lib64/libra.so \
  --npu-ip <npu-roce-ip> --logical-device 0 --physical-device 0 \
  --cpu-ip <cpu-roce-ip> --tcp-port <port> \
  --operation write --offset 0 --bytes 4096
```

Use `--operation read` to have the CPU RDMA Write a namespace range into the
NPU application buffer. Hardware invocations need a whole-process timeout.

## Reference Basis

The RA lifecycle and doorbell flow were derived from CANN-matched HCOMM source:
`ra_host.c`, `ra_hdc_rdma.c`, `rs_rdma.c`,
`transport_ibverbs.cc`, and `adapter_rts.cc`. NDS uses their ABI behavior but
does not link HCOMM or copy its implementation.
