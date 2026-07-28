#!/usr/bin/env sh
set -eu

build_type="${1:-Debug}"
platform="${2:-Linux-x86_64}"
project_root="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
build_root="${project_root}/build/${platform}/${build_type}"

printf 'MasterAI test platform: %s\nMasterAI test build type: %s\n' "${platform}" "${build_type}"
ctest --test-dir "${build_root}" --output-on-failure
