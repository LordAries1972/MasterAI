#!/usr/bin/env sh
set -eu
project_root="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
settings="${1:-${project_root}/config/settings.json}"
build_type="${2:-Release}"
# ADR-0004: was hardcoded to Linux-x86_64, which silently looked in the
# wrong build directory for Linux-arm64 or Darwin-arm64 (macOS) builds.
platform="${3:-Linux-x86_64}"
binary="${project_root}/build/${platform}/${build_type}/masterai"
test -x "${binary}" || { echo "MasterAI binary is missing: ${binary}" >&2; exit 1; }
exec "${binary}" configure "${settings}"
