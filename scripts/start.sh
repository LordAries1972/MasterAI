#!/usr/bin/env sh
# Starts MasterAI from validated settings and records its PID under the
# authoritative configured runtime root.
set -eu
project_root="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
settings="${1:-${project_root}/config/settings.json}"
build_type="${2:-Release}"
binary="${project_root}/build/Linux-x86_64/${build_type}/masterai"
test -x "${binary}" || { echo "MasterAI binary is missing: ${binary}" >&2; exit 1; }
if [ ! -f "${settings}" ]; then
    if [ ! -t 0 ]; then
        echo "Configuration is missing; non-interactive startup will not run the wizard." >&2
        exit 1
    fi
    "${binary}" configure "${settings}"
fi
# Reuse the native configuration parser rather than duplicating JSON handling.
runtime_root="$("${binary}" runtime-root "${settings}")"
case "${runtime_root}" in
    /*) ;;
    *) runtime_root="${project_root}/${runtime_root}" ;;
esac
run_root="${runtime_root}/run"
mkdir -p "${run_root}"
if [ "${3:-}" = "--foreground" ]; then
    exec "${binary}" serve "${settings}"
fi
"${binary}" serve "${settings}" </dev/null >>"${run_root}/masterai.log" 2>&1 &
server_pid=$!
printf '%s' "${server_pid}" >"${run_root}/masterai.pid"
printf 'MasterAI started with process ID %s.\n' "${server_pid}"
