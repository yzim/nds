#!/bin/sh
set -eu

if ! command -v pre-commit >/dev/null 2>&1; then
    echo "pre-commit is required; install it and ensure it is available in PATH" >&2
    exit 2
fi

exec pre-commit run clang-format-apply --all-files --hook-stage manual
