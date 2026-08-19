#!/usr/bin/env bash
set -euo pipefail

require_environment() {
    local required=(NDS_E2E_BUILD_DIR NDS_E2E_CANN_ROOT NDS_E2E_CPU_IP NDS_E2E_TCP_PORT
        NDS_E2E_DEVICE NDS_E2E_GID_INDEX NDS_E2E_TORCH_PYTHON)
    local name
    for name in "${required[@]}"; do
        if [[ -z "${!name:-}" ]]; then
            printf 'missing required E2E variable: %s\n' "${name}" >&2
            exit 2
        fi
    done
}

cleanup() {
    if [[ -n "${server_pid:-}" ]] && kill -0 "${server_pid}" 2>/dev/null; then
        kill "${server_pid}" 2>/dev/null || true
        wait "${server_pid}" 2>/dev/null || true
    fi
}

require_environment
build="${NDS_E2E_BUILD_DIR}"
cann="${NDS_E2E_CANN_ROOT}"
state_dir="${NDS_E2E_STATE_DIR:-${TMPDIR:-/tmp}/nds-e2e}"
bytes=4096
case_timeout=45s
server_pid=""
mkdir -p "${state_dir}"
case_dir="$(mktemp -d "${state_dir}/torch-storage.XXXXXX")"
server_log="${case_dir}/server.log"
client_log="${case_dir}/client.log"
trap cleanup EXIT

timeout "${case_timeout}" "${build}/nds_server" \
    --device "${NDS_E2E_DEVICE}" --gid-index "${NDS_E2E_GID_INDEX}" \
    --listen "${NDS_E2E_CPU_IP}" --tcp-port "${NDS_E2E_TCP_PORT}" \
    --namespace-bytes 1048576 --storage-requests 2 --verify-write-bytes "${bytes}" --log-level info \
    >"${server_log}" 2>&1 &
server_pid=$!
sleep 1

set +e
timeout "${case_timeout}" sudo -n env PATH="${PATH}" PYTHONPATH="${build}" \
    bash -s "${cann}" "${NDS_E2E_CPU_IP}:${NDS_E2E_TCP_PORT}" "${NDS_E2E_TORCH_PYTHON}" >"${client_log}" 2>&1 <<'PYTHON_SHELL'
source "$1/set_env.sh"
exec "$3" - "$2" <<'PYTHON'
import sys

import torch
import torch_npu
import nds_torch

torch.npu.set_device(0)
session = nds_torch.Session(sys.argv[1])
payload = torch.arange(4096, dtype=torch.uint8) ^ 0x5A
session.write(payload, 0)
output = torch.empty_like(payload)
session.read_(output, 0)
assert torch.equal(output, payload)
assert session.capacity == 1048576
PYTHON
PYTHON_SHELL
client_rc=$?
wait "${server_pid}"
server_rc=$?
set -e
server_pid=""

printf 'client=%s server=%s client_rc=%s server_rc=%s\n' \
    "${client_log}" "${server_log}" "${client_rc}" "${server_rc}"
[[ "${client_rc}" -eq 0 && "${server_rc}" -eq 0 ]]
