#!/usr/bin/env bash

nds_prepare_aicpu_overlay() {
    local case_dir="$1"
    local cann_root="$2"
    local build_dir="$3"
    local overlay="${case_dir}/overlay"

    mkdir -p "${overlay}/conf" "${overlay}/opp/vendors/nds/aicpu/config" \
        "${overlay}/opp/vendors/nds/aicpu/kernel"
    cp "${cann_root}/aarch64-linux/conf/ascend_package_load.ini" "${overlay}/conf/ascend_package_load.ini"
    chmod u+w "${overlay}/conf/ascend_package_load.ini"
    printf '\nname:aicpu_nds.tar.gz\ninstall_path:2\noptional:true\npackage_path:opp/vendors/nds/aicpu/kernel\nload_as_per_soc:false\n' \
        >>"${overlay}/conf/ascend_package_load.ini"
    cp "${build_dir}/aicpu/nds_aicpu_standard.json" "${overlay}/opp/vendors/nds/aicpu/config/"
    cp "${build_dir}/aicpu/aicpu_nds.tar.gz" "${overlay}/opp/vendors/nds/aicpu/kernel/"
    printf '%s\n' "${overlay}"
}
