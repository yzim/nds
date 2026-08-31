#!/usr/bin/env bash
set -euo pipefail

runner_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
support_dir="${runner_dir}/../support"
# shellcheck source=aicpu_overlay.sh
source "${support_dir}/aicpu_overlay.sh"

usage() {
    printf '%s\n' 'Usage: run_torch_storage.sh --backend-mode <ra|aiv|aicpu>' >&2
}

require_environment() {
    local required=(NDS_E2E_BUILD_DIR NDS_E2E_CANN_ROOT NDS_E2E_SOURCE_DIR NDS_E2E_SERVER_ADDRESS
        NDS_E2E_DEVICE NDS_E2E_GID_INDEX NDS_E2E_TORCH_PYTHON)
    local name
    for name in "${required[@]}"; do
        if [[ -z "${!name:-}" ]]; then
            printf 'missing required E2E variable: %s\n' "${name}" >&2
            exit 2
        fi
    done
}

validate_backend() {
    case "$1" in
        ra|aiv|aicpu) ;;
        *) printf 'unsupported backend: %s\n' "$1" >&2; exit 2 ;;
    esac
}

cleanup() {
    if [[ -n "${server_pid:-}" ]] && kill -0 "${server_pid}" 2>/dev/null; then
        kill "${server_pid}" 2>/dev/null || true
        wait "${server_pid}" 2>/dev/null || true
    fi
}

run_client() {
    local backend="$1"
    local case_dir="$2"
    local client_log="$3"
    local -a client=("${NDS_E2E_TORCH_PYTHON}" "${NDS_E2E_SOURCE_DIR}/examples/storage/torch_client.py"
        "${NDS_E2E_SERVER_ADDRESS}" --backend-mode "${backend}" --bytes "${bytes}")

    case "${backend}" in
        ra)
            client+=(--backend-artifact-path "${build}/libnds_ra_backend.so")
            timeout "${case_timeout}" sudo -n env "PATH=${PATH}" "PYTHONPATH=${build}/bin" \
                bash -lc 'source "$1/set_env.sh"; shift; exec "$@"' bash "${cann}" "${client[@]}" >"${client_log}" 2>&1
            ;;
        aiv)
            client+=(--backend-artifact-path "${build}/aiv/nds_aiv_kernel.o")
            timeout "${case_timeout}" sudo -n env "PATH=${PATH}" "PYTHONPATH=${build}/bin" \
                bash -lc 'source "$1/set_env.sh"; shift; exec "$@"' bash "${cann}" "${client[@]}" >"${client_log}" 2>&1
            ;;
        aicpu)
            local overlay
            overlay="$(nds_prepare_aicpu_overlay "${case_dir}" "${cann}" "${build}")"
            client+=(--backend-artifact-path "${overlay}/opp/vendors/nds/aicpu/config/nds_aicpu_standard.json")
            timeout "${case_timeout}" sudo -n env "PATH=${PATH}" "PYTHONPATH=${build}/bin" \
                unshare -m "${support_dir}/run_with_aicpu_package.sh" "${overlay}" "${cann}" "${client[@]}" \
                >"${client_log}" 2>&1
            ;;
    esac
}

backend=""
while [[ "$#" -gt 0 ]]; do
    case "$1" in
        --backend-mode) backend="${2:-}"; shift 2 ;;
        --help|-h) usage; exit 0 ;;
        *) printf 'unknown option: %s\n' "$1" >&2; usage; exit 2 ;;
    esac
done
if [[ -z "${backend}" ]]; then
    usage
    exit 2
fi
validate_backend "${backend}"

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

timeout "${case_timeout}" "${build}/bin/nds_storage_server" \
    --device "${NDS_E2E_DEVICE}" --gid-index "${NDS_E2E_GID_INDEX}" \
    --listen "${NDS_E2E_SERVER_ADDRESS}" \
    --namespace-bytes 1048576 --storage-commands 2 --log-level info \
    >"${server_log}" 2>&1 &
server_pid=$!
sleep 1

set +e
run_client "${backend}" "${case_dir}" "${client_log}"
client_rc=$?
wait "${server_pid}"
server_rc=$?
set -e
server_pid=""

printf 'backend=%s client=%s server=%s client_rc=%s server_rc=%s\n' \
    "${backend}" "${client_log}" "${server_log}" "${client_rc}" "${server_rc}"
[[ "${client_rc}" -eq 0 && "${server_rc}" -eq 0 ]]
