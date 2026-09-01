#!/usr/bin/env bash
set -euo pipefail

runner_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
export NDS_E2E_SUPPORT_DIR="${runner_dir}/../support"
# shellcheck source=../support/common.sh
source "${NDS_E2E_SUPPORT_DIR}/common.sh"
# shellcheck source=../support/aicpu_overlay.sh
source "${NDS_E2E_SUPPORT_DIR}/aicpu_overlay.sh"

usage() {
    printf '%s\n' 'Usage: run.sh --backend-mode <ra|aiv|aicpu>' >&2
}

die_usage() {
    printf '%s\n' "$1" >&2
    usage
    exit 2
}

parse_args() {
    backend=""
    while [[ "$#" -gt 0 ]]; do
        case "$1" in
            --backend-mode)
                [[ "$#" -ge 2 ]] || die_usage 'missing value for --backend-mode'
                backend="$2"
                shift 2
                ;;
            --help|-h)
                usage
                exit 0
                ;;
            *)
                die_usage "unknown option: $1"
                ;;
        esac
    done

    [[ -n "${backend}" ]] || die_usage 'missing --backend-mode'
    case "${backend}" in
        ra|aiv|aicpu) ;;
        *) die_usage "unsupported direct-verbs backend: ${backend}" ;;
    esac
}

start_server() {
    local -a server_command=(
        "${nds_e2e_build}/bin/nds_verbs_server"
        --device "${NDS_E2E_DEVICE}"
        --gid-index "${NDS_E2E_GID_INDEX}"
        --listen "${NDS_E2E_SERVER_ADDRESS}"
    )
    nds_e2e_start_server "${nds_e2e_server_log}" "${server_command[@]}"
}

run_client() {
    local -a client_command=(
        "${nds_e2e_build}/bin/nds_verbs_client"
        --backend-mode "${backend}"
        --logical-device 0
        --server "${NDS_E2E_SERVER_ADDRESS}"
    )
    nds_e2e_run_client "${backend}" "${nds_e2e_case_dir}" "${nds_e2e_client_log}" \
        "${client_command[@]}"
}

wait_for_case() {
    local client_rc
    local server_rc

    set +e
    run_client
    client_rc=$?
    wait "${nds_e2e_server_pid}"
    server_rc=$?
    set -e
    nds_e2e_server_pid=""

    printf 'layer=verbs operation=send-recv-write backend=%s client=%s server=%s client_rc=%s server_rc=%s\n' \
        "${backend}" "${nds_e2e_client_log}" "${nds_e2e_server_log}" "${client_rc}" "${server_rc}"
    [[ "${client_rc}" -eq 0 && "${server_rc}" -eq 0 ]]
}

main() {
    parse_args "$@"
    nds_e2e_initialize_case verbs send-recv-write "${backend}"
    trap nds_e2e_cleanup EXIT

    start_server
    sleep 1
    wait_for_case
}

main "$@"
