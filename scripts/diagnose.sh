#!/usr/bin/env sh
# Runs MasterAI security and hardware diagnostics against the runtime selected
# by the validated settings file.
set -eu
project_root="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
settings="${1:-${project_root}/config/settings.json}"
build_type="${2:-Release}"
binary="${project_root}/build/Linux-x86_64/${build_type}/masterai"
# Reuse native configuration parsing rather than maintaining a shell parser.
runtime_root="$("${binary}" runtime-root "${settings}")"
case "${runtime_root}" in
    /*) ;;
    *) runtime_root="${project_root}/${runtime_root}" ;;
esac
"${binary}" security-status "${runtime_root}"
"${binary}" probe "${project_root}"
