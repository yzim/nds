#!/usr/bin/env bash

nds_e2e_require_environment() {
    local required=(NDS_E2E_BUILD_DIR NDS_E2E_CANN_ROOT NDS_E2E_SERVER_ADDRESS NDS_E2E_DEVICE NDS_E2E_GID_INDEX)
    local name
    for name in "${required[@]}"; do
        if [[ -z "${!name:-}" ]]; then
            printf 'missing required E2E variable: %s\n' "${name}" >&2
            return 2
        fi
    done
}

nds_e2e_validate_backend() {
    case "$1" in
        ra|aiv|aicpu) ;;
        *) printf 'unsupported backend: %s\n' "$1" >&2; return 2 ;;
    esac
}

nds_e2e_initialize_case() {
    local layer="$1"
    local operation="$2"
    local backend="$3"
    nds_e2e_require_environment || return
    nds_e2e_validate_backend "${backend}" || return
    nds_e2e_build="${NDS_E2E_BUILD_DIR}"
    nds_e2e_cann="${NDS_E2E_CANN_ROOT}"
    nds_e2e_timeout=45s
    nds_e2e_server_pid=""
    local state_dir="${NDS_E2E_STATE_DIR:-${TMPDIR:-/tmp}/nds-e2e}"
    mkdir -p "${state_dir}"
    nds_e2e_case_dir="$(mktemp -d "${state_dir}/${layer}-${operation}-${backend}.XXXXXX")"
    nds_e2e_server_log="${nds_e2e_case_dir}/server.log"
    nds_e2e_client_log="${nds_e2e_case_dir}/client.log"
}

nds_e2e_cleanup() {
    if [[ -n "${nds_e2e_server_pid:-}" ]] && kill -0 "${nds_e2e_server_pid}" 2>/dev/null; then
        kill "${nds_e2e_server_pid}" 2>/dev/null || true
        wait "${nds_e2e_server_pid}" 2>/dev/null || true
    fi
}

nds_e2e_start_server() {
    local log="$1"
    shift
    timeout "${nds_e2e_timeout}" "$@" >"${log}" 2>&1 &
    nds_e2e_server_pid=$!
}

# Runs a layer-provided client command with the selected backend's target setup.
nds_e2e_run_client() {
    local backend="$1"
    local case_dir="$2"
    local log="$3"
    shift 3
    local -a client=("$@")
    case "${backend}" in
        ra)
            client+=(--backend-artifact-path "${nds_e2e_build}/libnds_ra_backend.so")
            timeout "${nds_e2e_timeout}" sudo -n env "PATH=${PATH}" bash -lc \
                'source "$1/set_env.sh"; shift; exec "$@"' bash "${nds_e2e_cann}" "${client[@]}" >"${log}" 2>&1
            ;;
        aiv)
            client+=(--backend-artifact-path "${nds_e2e_build}/aiv/nds_aiv_kernel.o")
            timeout "${nds_e2e_timeout}" sudo -n env "PATH=${PATH}" bash -lc \
                'source "$1/set_env.sh"; shift; exec "$@"' bash "${nds_e2e_cann}" "${client[@]}" >"${log}" 2>&1
            ;;
        aicpu)
            local overlay
            overlay="$(nds_prepare_aicpu_overlay "${case_dir}" "${nds_e2e_cann}" "${nds_e2e_build}")"
            client+=(--backend-artifact-path "${overlay}/opp/vendors/nds/aicpu/config/nds_aicpu_standard.json")
            timeout "${nds_e2e_timeout}" sudo -n unshare -m "${NDS_E2E_SUPPORT_DIR}/run_with_aicpu_package.sh" \
                "${overlay}" "${nds_e2e_cann}" "${client[@]}" >"${log}" 2>&1
            ;;
    esac
}
