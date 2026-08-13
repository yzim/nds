# AICPU Backend

The AICPU backend launches an NDS-owned standard CP1 kernel to post one storage
command Send through the CANN-matched HNS provider.

## Data Path

```text
Host: RaAiQpCreate(NORMAL) -> launch NdsAicpuRdmaPost
CP1:  dlopen(libhns-rdmav25.so) -> dlsym(ibv_exp_post_send) -> post Send
CPU:  Receive command -> RDMA Read/Write data -> RDMA Write completion record
Host: poll copied NPU completion record
```

The host process does not link or load `libhns-rdmav25.so`. The standard CP1
image resolves it on the NPU. A NORMAL AI QP is required because the provider
rings its normal-QP doorbell; provider-OP mode only returns doorbell metadata.

## Interfaces and Usage

- Request ABI: `src/npu_client/modes/aicpu/include/nds/aicpu_roce_abi.h`.
- CP1 source: `src/npu_client/modes/aicpu/device/nds_aicpu_rdma_post.aicpu`.
- Entry point: `NdsAicpuRdmaPost`.
- Package manifest: `src/npu_client/modes/aicpu/device/package/nds_aicpu_standard.json.in`.

Build on the matching target:

```sh
cmake -S . -B build-aicpu -DNDS_CANN_ROOT=<cann-root> -DNDS_BUILD_AICPU_KERNEL=ON
cmake --build build-aicpu --parallel
```

Install the generated package using CANN's supported customer-AICPU procedure,
then run the normal client arguments with `--submission-mode aicpu` and
`--aicpu-kernel-config <path>/nds_aicpu_standard.json`.

## Reference Basis and Limits

NDS used HCOMM's `RunTransportRoceTx`, device provider loader, AI-QP creation,
and HNS provider sources as ABI evidence. It ports only one provider post. It
does not wrap HCOMM's bundled transport kernel or its Rx-side flags,
synchronization, communicator, batching, or collective logic. As in all NDS
backends, the CPU-written completion record, not CP1 launch completion or an
internal AI-QP CQ, is the storage result.
