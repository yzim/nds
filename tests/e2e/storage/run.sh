#!/usr/bin/env bash
set -euo pipefail

runner_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
support_dir="${runner_dir}/../support"
# shellcheck source=aicpu_overlay.sh
source "${support_dir}/aicpu_overlay.sh"

usage() {
    cat <<'EOF'
Usage:
  run_storage.sh --backend-mode <ra|aiv|aicpu> --operation <read|write|batch-read|batch-write>
  run_storage.sh --sweep

Run one payload-verified NDS storage case, or sweep every backend and operation.
EOF
}

require_environment() {
    local required=(NDS_E2E_BUILD_DIR NDS_E2E_CANN_ROOT NDS_E2E_SERVER_ADDRESS NDS_E2E_DEVICE NDS_E2E_GID_INDEX)
    local name
    for name in "${required[@]}"; do
        if [[ -z "${!name:-}" ]]; then
            printf 'missing required E2E variable: %s\n' "${name}" >&2
            exit 2
        fi
    done
}

validate_case() {
    case "$1" in ra|aiv|aicpu) ;; *) printf 'unsupported backend: %s\n' "$1" >&2; exit 2 ;; esac
    case "$2" in read|write|batch-read|batch-write) ;; *) printf 'unsupported operation: %s\n' "$2" >&2; exit 2 ;; esac
}

cleanup() {
    if [[ -n "${server_pid:-}" ]] && kill -0 "${server_pid}" 2>/dev/null; then
        kill "${server_pid}" 2>/dev/null || true
        wait "${server_pid}" 2>/dev/null || true
    fi
}

run_client() {
    local backend="$1"
    local operation="$2"
    local case_dir="$3"
    local client_log="$4"
    local -a client=("${build}/bin/nds_storage_client" --backend-mode "${backend}"
        --ascendcl "${cann}/aarch64-linux/lib64/libascendcl.so"
        --logical-device 0
        --server "${NDS_E2E_SERVER_ADDRESS}"
        --operation "${operation}" --offset 0 --bytes "${bytes}" --log-level info)

    case "${backend}" in
        ra)
            client+=(--backend-artifact-path "${build}/libnds_ra_backend.so")
            timeout "${case_timeout}" sudo -n env "PATH=${PATH}" bash -lc \
                'source "$1/set_env.sh"; shift; exec "$@"' bash "${cann}" "${client[@]}" >"${client_log}" 2>&1
            ;;
        aiv)
            client+=(--backend-artifact-path "${build}/aiv/nds_aiv_kernel.o")
            timeout "${case_timeout}" sudo -n env "PATH=${PATH}" bash -lc \
                'source "$1/set_env.sh"; shift; exec "$@"' bash "${cann}" "${client[@]}" >"${client_log}" 2>&1
            ;;
        aicpu)
            local overlay
            overlay="$(nds_prepare_aicpu_overlay "${case_dir}" "${cann}" "${build}")"
            client+=(--backend-artifact-path "${overlay}/opp/vendors/nds/aicpu/config/nds_aicpu_standard.json")
            timeout "${case_timeout}" sudo -n unshare -m "${support_dir}/run_with_aicpu_package.sh" \
                "${overlay}" "${cann}" "${client[@]}" >"${client_log}" 2>&1
            ;;
    esac
}

run_case() {
    local backend="$1"
    local operation="$2"
    validate_case "${backend}" "${operation}"

    local case_dir
    case_dir="$(mktemp -d "${state_dir}/${backend}-${operation}.XXXXXX")"
    local server_log="${case_dir}/server.log"
    local client_log="${case_dir}/client.log"
    local verify_bytes
    local -a server=("${build}/bin/nds_storage_server" --device "${NDS_E2E_DEVICE}" --gid-index "${NDS_E2E_GID_INDEX}"
        --listen "${NDS_E2E_SERVER_ADDRESS}" --namespace-bytes 1048576 --log-level info)

    if [[ "${operation}" == read || "${operation}" == batch-read ]]; then
        server+=(--seed-pattern)
    else
        verify_bytes="${bytes}"
        if [[ "${operation}" == batch-write ]]; then
            verify_bytes="$((bytes * 2))"
        fi
        server+=(--verify-write-bytes "${verify_bytes}")
    fi

    timeout "${case_timeout}" "${server[@]}" >"${server_log}" 2>&1 &
    server_pid=$!
    sleep 1

    set +e
    run_client "${backend}" "${operation}" "${case_dir}" "${client_log}"
    local client_rc=$?
    wait "${server_pid}"
    local server_rc=$?
    set -e
    server_pid=""

    printf 'backend=%s operation=%s client_rc=%s server_rc=%s logs=%s\n' \
        "${backend}" "${operation}" "${client_rc}" "${server_rc}" "${case_dir}"
    [[ "${client_rc}" -eq 0 && "${server_rc}" -eq 0 ]]
}

backend=""
operation=""
sweep=false
while [[ "$#" -gt 0 ]]; do
    case "$1" in
        --backend-mode) backend="${2:-}"; shift 2 ;;
        --operation) operation="${2:-}"; shift 2 ;;
        --sweep) sweep=true; shift ;;
        --help|-h) usage; exit 0 ;;
        *) printf 'unknown option: %s\n' "$1" >&2; usage >&2; exit 2 ;;
    esac
done

require_environment
build="${NDS_E2E_BUILD_DIR}"
cann="${NDS_E2E_CANN_ROOT}"
state_dir="${NDS_E2E_STATE_DIR:-${TMPDIR:-/tmp}/nds-e2e}"
bytes=4096
case_timeout=45s
server_pid=""
mkdir -p "${state_dir}"
trap cleanup EXIT

if [[ "${sweep}" == true ]]; then
    if [[ -n "${backend}" || -n "${operation}" ]]; then
        printf '%s\n' '--sweep cannot be combined with --backend-mode or --operation' >&2
        exit 2
    fi
    for backend in ra aiv aicpu; do
        for operation in read write batch-read batch-write; do
            run_case "${backend}" "${operation}"
        done
    done
else
    if [[ -z "${backend}" || -z "${operation}" ]]; then
        usage >&2
        exit 2
    fi
    run_case "${backend}" "${operation}"
fi
