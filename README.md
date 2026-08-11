# NDS

NDS is a small experiment for bringing up a direct RoCE connection between one Ascend NPU RNIC and one Mellanox RNIC on the host.

The two sides intentionally stay simple:

- **NPU:** CANN's RA interface from `libra.so`.
- **CPU:** plain `libibverbs`.
- **Control plane:** a small TCP exchange owned by this project.

This is a one-NPU-to-one-CPU setup. It is not an HCCL job and does not need HCOMM, rank tables, or a second NPU in the normal path.

## What works today

The QP bring-up path has been tested on the target CANN release.

1. The NPU creates its ACL/runtime/RA context and one RC QP.
2. The CPU server creates one `libibverbs` RC QP and moves it through `INIT`, `RTR`, and `RTS`.
3. The two programs exchange the QP details they need over TCP.
4. The NPU passes the CPU endpoint information to `RaTypicalQpModify`.
5. Both QPs are torn down cleanly.

This is only a connection test. There is no memory registration and no data transfer yet—no RDMA read, write, send, or receive work request is posted.

## How it fits together

```text
NPU client                                      CPU server
----------                                      ----------
AscendCL → CANN runtime → RA (`libra.so`)       libibverbs
       │                                                │
create RA rdev and RC QP                         create RC QP
       │                                                │
       └──── project-owned TCP endpoint exchange ──────┘
                         │
              RaTypicalQpModify / RTR + RTS
```

## Open-source references

Thanks to the contributors to [HCCL](https://gitcode.com/cann/hccl) and [HCOMM](https://gitcode.com/cann/hcomm). The lifecycle, transport, and ABI details used here are based on information already public in those projects. HCOMM includes the HCCP source that is useful when checking RA behavior.

At runtime, NDS uses the CANN libraries installed on the machine. Those installed libraries define the ABI that the NPU program actually calls.

For the QP path, the NPU side follows this order:

```text
aclInit
→ aclrtSetDevice
→ rtOpenNetService(--hdcType=18)
→ RaInit
→ RaRdevInit(NETWORK_OFFLINE, NOTIFY, ...)
→ RaTypicalQpCreate
→ endpoint exchange
→ RaTypicalQpModify
→ RaQpDestroy
→ RaRdevDeinit(..., NOTIFY)
→ RaDeinit
→ rtCloseNetService
→ aclFinalize
```

For an offline HDC rdev, `NOTIFY` is `1`. `NO_USE` (`0`) does not work for this path.

## Layout

```text
src/common/             TCP control plane and endpoint wire format
src/cpu_server/         CPU-side libibverbs server
src/npu_client/         NPU-side CANN/RA client
  loaders/              Dynamic CANN ABI loaders
include/nds/            Project headers
tests/                  Host-runnable protocol and wrapper tests
```

## Build

You can build and run the host-side tests without CANN hardware:

```sh
cmake -S . -B build
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

The CPU server is built only when `libibverbs` development files are available. The NPU client takes absolute library paths at runtime so it can use one selected CANN installation.

## Run the QP test

Start the CPU server with its RDMA device and GID index:

```sh
nds_verbs_server --device <rdma-device> --gid-index <index> \
  [--listen <cpu-ipv4>] [--tcp-port <port>] [--ib-port <port>]
```

Then start the NPU client with one selected NPU and the matching CANN libraries:

```sh
nds_npu_qp_client \
  --ascendcl <path-to-libascendcl.so> \
  --runtime <path-to-libruntime.so> \
  --ra <path-to-libra.so> \
  --npu-ip <npu-rnic-ipv4> \
  --logical-device <id> --physical-device <id> \
  --cpu-ip <cpu-rnic-ipv4> --execute
```

Use a whole-process timeout when running accelerator experiments. Keep this test to one NPU and one CPU RNIC. Put machine-specific commands and deployment values in ignored `.local/`, not in this README.

## Next step

The next milestone is one small, bounded RDMA write from the NPU to the CPU: register memory on both sides, exchange only the public memory descriptor fields needed by the test, post one operation, and check the payload on the CPU. The CPU side will remain plain `libibverbs`.
