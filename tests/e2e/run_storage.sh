#!/usr/bin/env bash
set -euo pipefail

usage() {
    cat <<'EOF'
Usage:
  run_storage.sh --backend <ra|aiv|aicpu> --operation <read|write>
  run_storage.sh --sweep

Run one payload-verified NDS storage case, or sweep every backend and operation.
EOF
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

validate_case() {
    case "$1" in ra|aiv|aicpu) ;; *) printf 'unsupported backend: %s\n' "$1" >&2; exit 2 ;; esac
    case "$2" in read|write) ;; *) printf 'unsupported operation: %s\n' "$2" >&2; exit 2 ;; esac
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
    local -a client=("${build}/nds_client" --execution "${backend}"
        --ascendcl "${cann}/aarch64-linux/lib64/libascendcl.so"
        --runtime "${cann}/aarch64-linux/lib64/libruntime.so"
        --ra "${cann}/aarch64-linux/lib64/libra.so"
        --npu-ip "${NDS_E2E_NPU_IP}" --logical-device 0 --physical-device 0
        --cpu-ip "${NDS_E2E_CPU_IP}" --tcp-port "${NDS_E2E_TCP_PORT}"
        --operation "${operation}" --offset 0 --bytes "${bytes}" --log-level info)

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
            local overlay="${case_dir}/overlay"
            mkdir -p "${overlay}/conf" "${overlay}/opp/vendors/nds/aicpu/config" \
                "${overlay}/opp/vendors/nds/aicpu/kernel"
            cp "${cann}/aarch64-linux/conf/ascend_package_load.ini" "${overlay}/conf/ascend_package_load.ini"
            chmod u+w "${overlay}/conf/ascend_package_load.ini"
            printf '\nname:aicpu_nds.tar.gz\ninstall_path:2\noptional:true\npackage_path:opp/vendors/nds/aicpu/kernel\nload_as_per_soc:false\n' \
                >>"${overlay}/conf/ascend_package_load.ini"
            cp "${build}/aicpu/nds_aicpu_standard.json" "${overlay}/opp/vendors/nds/aicpu/config/"
            cp "${build}/aicpu/aicpu_nds.tar.gz" "${overlay}/opp/vendors/nds/aicpu/kernel/"
            client+=(--aicpu-kernel-config "${overlay}/opp/vendors/nds/aicpu/config/nds_aicpu_standard.json")
            timeout "${case_timeout}" sudo -n unshare -m "${runner_dir}/run_with_aicpu_package.sh" \
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
    local -a server=("${build}/nds_server" --device "${NDS_E2E_DEVICE}" --gid-index "${NDS_E2E_GID_INDEX}"
        --listen "${NDS_E2E_CPU_IP}" --tcp-port "${NDS_E2E_TCP_PORT}" --namespace-bytes 1048576 --log-level info)

    if [[ "${operation}" == read ]]; then
        server+=(--seed-pattern)
    else
        server+=(--verify-write-bytes "${bytes}")
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
        --backend) backend="${2:-}"; shift 2 ;;
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
runner_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
bytes=4096
case_timeout=45s
server_pid=""
mkdir -p "${state_dir}"
trap cleanup EXIT

if [[ "${sweep}" == true ]]; then
    if [[ -n "${backend}" || -n "${operation}" ]]; then
        printf '%s\n' '--sweep cannot be combined with --backend or --operation' >&2
        exit 2
    fi
    for backend in ra aiv aicpu; do
        for operation in read write; do
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
