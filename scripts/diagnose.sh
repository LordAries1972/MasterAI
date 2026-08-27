#!/usr/bin/env sh
# Runs MasterAI security and hardware diagnostics against the runtime selected
# by the validated settings file.
set -eu
project_root="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
settings="${1:-${project_root}/config/settings.json}"
build_type="${2:-Release}"
# ADR-0004: was hardcoded to Linux-x86_64, which silently looked in the
# wrong build directory for Linux-arm64 or Darwin-arm64 (macOS) builds.
platform="${3:-Linux-x86_64}"
binary="${project_root}/build/${platform}/${build_type}/masterai"
# Reuse native configuration parsing rather than maintaining a shell parser.
runtime_root="$("${binary}" runtime-root "${settings}")"
case "${runtime_root}" in
    /*) ;;
    *) runtime_root="${project_root}/${runtime_root}" ;;
esac
"${binary}" security-status "${runtime_root}"
"${binary}" probe "${project_root}"
