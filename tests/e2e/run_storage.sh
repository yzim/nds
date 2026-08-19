#!/usr/bin/env bash
set -u -o pipefail

required=(NDS_E2E_MODE NDS_E2E_BUILD_DIR NDS_E2E_CANN_ROOT NDS_E2E_NPU_IP
    NDS_E2E_CPU_IP NDS_E2E_TCP_PORT NDS_E2E_DEVICE NDS_E2E_GID_INDEX)
for name in "${required[@]}"; do
    if [[ -z "${!name:-}" ]]; then
        printf 'missing required E2E variable: %s\n' "${name}" >&2
        exit 2
    fi
done

case "${NDS_E2E_MODE}" in
    ra|aiv|aicpu) ;;
    *) printf 'unsupported E2E mode: %s\n' "${NDS_E2E_MODE}" >&2; exit 2 ;;
esac

build="${NDS_E2E_BUILD_DIR}"
cann="${NDS_E2E_CANN_ROOT}"
state_dir="${NDS_E2E_STATE_DIR:-${TMPDIR:-/tmp}/nds-e2e}"
mkdir -p "${state_dir}"
case_dir="$(mktemp -d "${state_dir}/${NDS_E2E_MODE}-storage.XXXXXX")"
server_log="${case_dir}/server.log"
client_log="${case_dir}/client.log"
server_pid=""

cleanup() {
    if [[ -n "${server_pid}" ]] && kill -0 "${server_pid}" 2>/dev/null; then
        kill "${server_pid}" 2>/dev/null || true
        wait "${server_pid}" 2>/dev/null || true
    fi
}
trap cleanup EXIT

timeout 45s "${build}/nds_server" \
    --device "${NDS_E2E_DEVICE}" --gid-index "${NDS_E2E_GID_INDEX}" \
    --listen "${NDS_E2E_CPU_IP}" --tcp-port "${NDS_E2E_TCP_PORT}" \
    --namespace-bytes 1048576 --verify-write-bytes 4096 --log-level info \
    >"${server_log}" 2>&1 &
server_pid=$!
sleep 1

client_args=(--execution "${NDS_E2E_MODE}"
    --ascendcl "${cann}/aarch64-linux/lib64/libascendcl.so"
    --runtime "${cann}/aarch64-linux/lib64/libruntime.so"
    --ra "${cann}/aarch64-linux/lib64/libra.so"
    --npu-ip "${NDS_E2E_NPU_IP}" --logical-device 0 --physical-device 0
    --cpu-ip "${NDS_E2E_CPU_IP}" --tcp-port "${NDS_E2E_TCP_PORT}"
    --operation write --offset 0 --bytes 4096 --log-level info)

set +e
if [[ "${NDS_E2E_MODE}" == aiv ]]; then
    timeout 45s sudo -n "${build}/nds_client" "${client_args[@]}" \
        --aiv-kernel "${build}/aiv/nds_aiv_kernel.o" >"${client_log}" 2>&1
elif [[ "${NDS_E2E_MODE}" == aicpu ]]; then
    overlay="${case_dir}/overlay"
    mkdir -p "${overlay}/conf" "${overlay}/opp/vendors/nds/aicpu/config" \
        "${overlay}/opp/vendors/nds/aicpu/kernel"
    cp "${cann}/aarch64-linux/conf/ascend_package_load.ini" "${overlay}/conf/ascend_package_load.ini"
    chmod u+w "${overlay}/conf/ascend_package_load.ini"
    printf '\nname:aicpu_nds.tar.gz\ninstall_path:2\noptional:true\npackage_path:opp/vendors/nds/aicpu/kernel\nload_as_per_soc:false\n' \
        >>"${overlay}/conf/ascend_package_load.ini"
    cp "${build}/aicpu/nds_aicpu_standard.json" "${overlay}/opp/vendors/nds/aicpu/config/"
    cp "${build}/aicpu/aicpu_nds.tar.gz" "${overlay}/opp/vendors/nds/aicpu/kernel/"
    timeout 45s sudo -n unshare -m "${BASH_SOURCE[0]%/*}/run_with_aicpu_package.sh" \
        "${overlay}" "${cann}" "${build}/nds_client" "${client_args[@]}" \
        --aicpu-kernel-config "${overlay}/opp/vendors/nds/aicpu/config/nds_aicpu_standard.json" \
        >"${client_log}" 2>&1
else
    timeout 45s sudo -n "${build}/nds_client" "${client_args[@]}" >"${client_log}" 2>&1
fi
client_rc=$?
wait "${server_pid}"
server_rc=$?
set -e
server_pid=""

printf 'mode=%s client_rc=%s server_rc=%s logs=%s\n' \
    "${NDS_E2E_MODE}" "${client_rc}" "${server_rc}" "${case_dir}"
[[ "${client_rc}" -eq 0 && "${server_rc}" -eq 0 ]]
