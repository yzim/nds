#!/usr/bin/env python3
"""Run a bounded NDS storage round trip through the PyTorch client."""

import argparse

import torch
import torch_npu

import _nds_torch


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("server", help="CPU storage server as IPv4:port")
    parser.add_argument("--backend-mode", choices=("ra", "aiv", "aicpu"), default="ra")
    parser.add_argument("--backend-artifact", required=True)
    parser.add_argument("--bytes", type=int, default=4096)
    args = parser.parse_args()
    if args.bytes <= 0:
        parser.error("--bytes must be positive")

    torch.npu.set_device(0)
    session = _nds_torch.Session(args.server, args.backend_mode, args.backend_artifact)
    payload = torch.arange(args.bytes, dtype=torch.int64).remainder(256).to(torch.uint8)
    session.write(payload, 0)
    output = torch.empty_like(payload)
    session.read_(output, 0)
    if not torch.equal(output, payload):
        raise RuntimeError("NDS storage round trip mismatch")
    print(f"NDS storage round trip passed: {args.bytes} bytes, capacity={session.capacity}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
