#!/usr/bin/env sh
set -eu

build_type="${1:-Debug}"
platform="${2:-Linux-x86_64}"

case "${build_type}" in
    Debug|Release) ;;
    *) echo "Build type must be Debug or Release." >&2; exit 2 ;;
esac

case "${platform}" in
    Linux-x86_64|Linux-arm64) ;;
    *) echo "Platform must be Linux-x86_64 or Linux-arm64." >&2; exit 2 ;;
esac

project_root="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
build_root="${project_root}/build/${platform}/${build_type}"

printf 'MasterAI platform: %s\nMasterAI build type: %s\n' "${platform}" "${build_type}"
cmake -S "${project_root}/scripts" -B "${build_root}" -DCMAKE_BUILD_TYPE="${build_type}"
cmake --build "${build_root}" --parallel
