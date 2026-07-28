#!/usr/bin/env sh
# Removes only MasterAI's generated build tree; source, models, and runtime data
# remain untouched. Pass --dry-run to preview the operation.
set -eu

case "${1:-}" in
    "") dry_run=0 ;;
    --dry-run) dry_run=1 ;;
    *)
        echo "Usage: $0 [--dry-run]" >&2
        exit 2
        ;;
esac

project_root="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
build_root="${project_root}/build"

case "${build_root}" in
    "${project_root}/build") ;;
    *)
        echo "Refusing to clean an unexpected path: ${build_root}" >&2
        exit 1
        ;;
esac

if [ ! -e "${build_root}" ]; then
    printf 'MasterAI build tree is already clean: %s\n' "${build_root}"
    exit 0
fi

if [ "${dry_run}" -eq 1 ]; then
    printf 'Would remove MasterAI build tree: %s\n' "${build_root}"
    exit 0
fi

rm -rf -- "${build_root}"
printf 'Removed MasterAI build tree: %s\n' "${build_root}"
