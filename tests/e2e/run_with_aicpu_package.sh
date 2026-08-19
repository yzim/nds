#!/usr/bin/env bash
set -euo pipefail

if [[ "$#" -lt 4 ]]; then
    printf 'usage: %s <overlay> <cann-root> <client> [client-args...]\n' "$0" >&2
    exit 2
fi

overlay="$1"
cann_root="$2"
client="$3"
shift 3

mount --bind "${overlay}/conf/ascend_package_load.ini" \
    "${cann_root}/aarch64-linux/conf/ascend_package_load.ini"
mount --bind "${overlay}/opp/vendors" "${cann_root}/opp/vendors"
set +u
source "${cann_root}/set_env.sh"
set -u
exec "${client}" "$@"
