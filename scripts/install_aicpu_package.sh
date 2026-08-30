#!/usr/bin/env bash
# Install or remove the NDS AICPU package from one CANN release.
#
# NDS follows HCOMM's CPU-kernel mode-0 package path. It requires both the
# vendor manifest and a registered kernel archive in CANN's opp tree. This
# script makes that deployment explicit; it never uses an overlay.

set -euo pipefail

usage() {
    cat <<'EOF'
Usage:
  install_aicpu_package.sh --cann-root <path> --package-dir <path>
  install_aicpu_package.sh --cann-root <path> --uninstall

Install expects these AICPU package build outputs in --package-dir:
  nds_aicpu_standard.json
  aicpu_nds.tar.gz

The script installs only:
  <cann-root>/opp/vendors/nds/aicpu/config/nds_aicpu_standard.json
  <cann-root>/opp/vendors/nds/aicpu/kernel/aicpu_nds.tar.gz

It adds/removes only NDS's exact aicpu_nds.tar.gz entry in
<cann-root>/aarch64-linux/conf/ascend_package_load.ini.  Elevated privilege is
required because the CANN installation is normally system-owned.
EOF
}

cann_root=''
package_dir=''
uninstall=false

while (($# > 0)); do
    case "$1" in
        --cann-root)
            cann_root=${2:?--cann-root requires a path}
            shift 2
            ;;
        --package-dir)
            package_dir=${2:?--package-dir requires a path}
            shift 2
            ;;
        --uninstall)
            uninstall=true
            shift
            ;;
        --help|-h)
            usage
            exit 0
            ;;
        *)
            printf 'unknown argument: %s\n' "$1" >&2
            usage >&2
            exit 2
            ;;
    esac
done

if [[ -z "$cann_root" ]] || { [[ "$uninstall" == false ]] && [[ -z "$package_dir" ]]; }; then
    usage >&2
    exit 2
fi

ini_file="$cann_root/aarch64-linux/conf/ascend_package_load.ini"
vendor_root="$cann_root/opp/vendors/nds/aicpu"
manifest_dest="$vendor_root/config/nds_aicpu_standard.json"
archive_dest="$vendor_root/kernel/aicpu_nds.tar.gz"

if [[ ! -f "$ini_file" ]]; then
    printf 'CANN package configuration does not exist: %s\n' "$ini_file" >&2
    exit 1
fi

if [[ "$uninstall" == false ]]; then
    manifest_src="$package_dir/nds_aicpu_standard.json"
    archive_src="$package_dir/aicpu_nds.tar.gz"
    for source_file in "$manifest_src" "$archive_src"; do
        if [[ ! -f "$source_file" ]]; then
            printf 'AICPU package artifact does not exist: %s\n' "$source_file" >&2
            exit 1
        fi
    done
fi

# Use a private temporary copy, then replace the configuration once.  The awk
# rule deletes only the NDS stanza, so existing CANN package registrations are
# preserved on install and uninstall.
temporary_ini=$(mktemp)
trap 'rm -f "$temporary_ini"' EXIT

awk '
    $0 == "name:aicpu_nds.tar.gz" {
        skip = 4
        next
    }
    skip > 0 {
        --skip
        next
    }
    { print }
' "$ini_file" >"$temporary_ini"

if [[ "$uninstall" == false ]]; then
    cat >>"$temporary_ini" <<'EOF'
name:aicpu_nds.tar.gz
install_path:2
optional:true
package_path:opp/vendors/nds/aicpu/kernel
load_as_per_soc:false
EOF
fi

if [[ "$uninstall" == true ]]; then
    sudo -n rm -f "$manifest_dest" "$archive_dest"
    sudo -n rmdir --ignore-fail-on-non-empty "$vendor_root/config" "$vendor_root/kernel" "$vendor_root" 2>/dev/null || true
else
    sudo -n install -d -m 0755 "$vendor_root/config" "$vendor_root/kernel"
    sudo -n install -m 0644 "$manifest_src" "$manifest_dest"
    sudo -n install -m 0644 "$archive_src" "$archive_dest"
fi

sudo -n install -m 0644 "$temporary_ini" "$ini_file"

if [[ "$uninstall" == true ]]; then
    printf 'Removed NDS AICPU package registration from %s\n' "$cann_root"
else
    printf 'Installed NDS AICPU package into %s\n' "$cann_root"
fi
