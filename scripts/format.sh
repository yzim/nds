#!/bin/sh
set -eu

if ! command -v pre-commit >/dev/null 2>&1; then
    case "$(uname -s)" in
        Darwin)
            echo "pre-commit is required; install it with 'brew install pre-commit' or 'pipx install pre-commit'" >&2
            ;;
        Linux)
            echo "pre-commit is required; install it with 'pipx install pre-commit' or your package manager" >&2
            ;;
        *)
            echo "pre-commit is required; install it with 'pipx install pre-commit'" >&2
            ;;
    esac
    exit 2
fi

exec pre-commit run clang-format-apply --all-files --hook-stage manual
