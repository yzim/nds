#!/usr/bin/env bash
set -euo pipefail

runner_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=aicpu_overlay.sh
source "${runner_dir}/aicpu_overlay.sh"

usage() {
    printf '%s\n' 'Usage: run_example.sh --layer <verbs|transport> --backend <ra|aiv|aicpu>' >&2
}

require_environment() {
    local required=(NDS_E2E_BUILD_DIR NDS_E2E_CANN_ROOT NDS_E2E_NPU_IP NDS_E2E_CPU_IP
        NDS_E2E_TCP_PORT NDS_E2E_DEVICE NDS_E2E_GID_INDEX)
    local name
    for name in "${required[@]}"; do
        if [[ -z "${!name:-}" ]]; then
            printf 'missing required E2E variable: %s\n' "${name}" >&2
            exit 2
        fi
    done
}

validate_value() {
    case "$1" in
        verbs|transport|ra|aiv|aicpu) ;;
        *) printf 'unsupported value: %s\n' "$1" >&2; exit 2 ;;
    esac
}

cleanup() {
    if [[ -n "${server_pid:-}" ]] && kill -0 "${server_pid}" 2>/dev/null; then
        kill "${server_pid}" 2>/dev/null || true
        wait "${server_pid}" 2>/dev/null || true
    fi
}

run_client() {
    local layer="$1"
    local backend="$2"
    local case_dir="$3"
    local client_log="$4"
    local -a client=("${build}/bin/nds_${layer}_client" --backend "${backend}"
        --ascendcl "${cann}/aarch64-linux/lib64/libascendcl.so"
        --runtime "${cann}/aarch64-linux/lib64/libruntime.so"
        --ra "${cann}/aarch64-linux/lib64/libra.so"
        --npu-ip "${NDS_E2E_NPU_IP}" --logical-device 0 --physical-device 0
        --cpu-ip "${NDS_E2E_CPU_IP}" --tcp-port "${NDS_E2E_TCP_PORT}")

    case "${backend}" in
        ra)
            timeout "${case_timeout}" sudo -n env "PATH=${PATH}" bash -lc \
                'source "$1/set_env.sh"; shift; exec "$@"' bash "${cann}" "${client[@]}" >"${client_log}" 2>&1
            ;;
        aiv)
            client+=(--aiv-kernel "${build}/aiv/nds_aiv_kernel.o")
            timeout "${case_timeout}" sudo -n env "PATH=${PATH}" bash -lc \
                'source "$1/set_env.sh"; shift; exec "$@"' bash "${cann}" "${client[@]}" >"${client_log}" 2>&1
            ;;
        aicpu)
            local overlay
            overlay="$(nds_prepare_aicpu_overlay "${case_dir}" "${cann}" "${build}")"
            client+=(--aicpu-kernel-config "${overlay}/opp/vendors/nds/aicpu/config/nds_aicpu_standard.json")
            timeout "${case_timeout}" sudo -n unshare -m "${runner_dir}/run_with_aicpu_package.sh" \
                "${overlay}" "${cann}" "${client[@]}" >"${client_log}" 2>&1
            ;;
    esac
}

layer=""
backend=""
while [[ "$#" -gt 0 ]]; do
    case "$1" in
        --layer) layer="${2:-}"; shift 2 ;;
        --backend) backend="${2:-}"; shift 2 ;;
        --help|-h) usage; exit 0 ;;
        *) printf 'unknown option: %s\n' "$1" >&2; usage; exit 2 ;;
    esac
done
if [[ -z "${layer}" || -z "${backend}" ]]; then
    usage
    exit 2
fi
validate_value "${layer}"
validate_value "${backend}"

require_environment
build="${NDS_E2E_BUILD_DIR}"
cann="${NDS_E2E_CANN_ROOT}"
state_dir="${NDS_E2E_STATE_DIR:-${TMPDIR:-/tmp}/nds-e2e}"
case_timeout=45s
server_pid=""
mkdir -p "${state_dir}"
case_dir="$(mktemp -d "${state_dir}/${layer}-${backend}.XXXXXX")"
server_log="${case_dir}/server.log"
client_log="${case_dir}/client.log"
trap cleanup EXIT

timeout "${case_timeout}" "${build}/bin/nds_${layer}_server" \
    --device "${NDS_E2E_DEVICE}" --gid-index "${NDS_E2E_GID_INDEX}" \
    --listen "${NDS_E2E_CPU_IP}" --tcp-port "${NDS_E2E_TCP_PORT}" --log-level info >"${server_log}" 2>&1 &
server_pid=$!
sleep 1

set +e
run_client "${layer}" "${backend}" "${case_dir}" "${client_log}"
client_rc=$?
wait "${server_pid}"
server_rc=$?
set -e
server_pid=""

printf 'layer=%s backend=%s client=%s server=%s client_rc=%s server_rc=%s\n' \
    "${layer}" "${backend}" "${client_log}" "${server_log}" "${client_rc}" "${server_rc}"
[[ "${client_rc}" -eq 0 && "${server_rc}" -eq 0 ]]
