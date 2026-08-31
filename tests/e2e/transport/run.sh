#!/usr/bin/env bash
set -euo pipefail

runner_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
export NDS_E2E_SUPPORT_DIR="${runner_dir}/../support"
# shellcheck source=../support/common.sh
source "${NDS_E2E_SUPPORT_DIR}/common.sh"
# shellcheck source=../support/aicpu_overlay.sh
source "${NDS_E2E_SUPPORT_DIR}/aicpu_overlay.sh"

usage() {
    printf '%s\n' 'Usage: run.sh --backend-mode <ra|aiv|aicpu> --operation <send|recv|read|write>' >&2
}

validate_operation() {
    case "$1" in
        send|recv|read|write) ;;
        *) printf 'unsupported transport operation: %s\n' "$1" >&2; return 2 ;;
    esac
}

backend=""
operation=""
while [[ "$#" -gt 0 ]]; do
    case "$1" in
        --backend-mode) backend="${2:-}"; shift 2 ;;
        --operation) operation="${2:-}"; shift 2 ;;
        --help|-h) usage; exit 0 ;;
        *) printf 'unknown option: %s\n' "$1" >&2; usage; exit 2 ;;
    esac
done
if [[ -z "${backend}" || -z "${operation}" ]]; then
    usage
    exit 2
fi
validate_operation "${operation}" || exit $?
nds_e2e_initialize_case transport "${operation}" "${backend}"
trap nds_e2e_cleanup EXIT

server=("${nds_e2e_build}/bin/nds_transport_server"
    --device "${NDS_E2E_DEVICE}" --gid-index "${NDS_E2E_GID_INDEX}"
    --listen "${NDS_E2E_SERVER_ADDRESS}" --operation "${operation}" --log-level info)
nds_e2e_start_server "${nds_e2e_server_log}" "${server[@]}"
sleep 1

client=("${nds_e2e_build}/bin/nds_transport_client" --backend-mode "${backend}"
    --ascendcl "${nds_e2e_cann}/aarch64-linux/lib64/libascendcl.so"
    --logical-device 0 --server "${NDS_E2E_SERVER_ADDRESS}" --operation "${operation}")
if [[ "${backend}" == aiv ]]; then
    client+=(--caller-polls-cq)
fi

set +e
nds_e2e_run_client "${backend}" "${nds_e2e_case_dir}" "${nds_e2e_client_log}" "${client[@]}"
client_rc=$?
wait "${nds_e2e_server_pid}"
server_rc=$?
set -e
nds_e2e_server_pid=""

printf 'layer=transport operation=%s backend=%s client=%s server=%s client_rc=%s server_rc=%s\n' \
    "${operation}" "${backend}" "${nds_e2e_client_log}" "${nds_e2e_server_log}" "${client_rc}" "${server_rc}"
[[ "${client_rc}" -eq 0 && "${server_rc}" -eq 0 ]]
