#!/bin/sh
set -eu

mode=apply

case "${1-}" in
    "")
        ;;
    --check)
        mode=check
        ;;
    *)
        echo "usage: $0 [--check]" >&2
        exit 2
        ;;
esac

if ! command -v pre-commit >/dev/null 2>&1; then
    echo "pre-commit is required; install it and ensure it is available in PATH" >&2
    exit 2
fi

if [ "$mode" = check ]; then
    exec pre-commit run clang-format --all-files --show-diff-on-failure
fi

exec pre-commit run clang-format-apply --all-files --hook-stage manual
