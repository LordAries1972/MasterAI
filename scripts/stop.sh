#!/usr/bin/env sh
# Requests graceful MasterAI shutdown through the configured runtime control
# file and waits for the recorded process to exit.
set -eu
project_root="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
settings="${1:-${project_root}/config/settings.json}"
build_type="${2:-Release}"
binary="${project_root}/build/Linux-x86_64/${build_type}/masterai"
test -x "${binary}" || { echo "MasterAI binary is missing: ${binary}" >&2; exit 1; }
# Resolve the same native runtime root used by start and server operations.
runtime_root="$("${binary}" runtime-root "${settings}")"
case "${runtime_root}" in
    /*) ;;
    *) runtime_root="${project_root}/${runtime_root}" ;;
esac
pid_file="${runtime_root}/run/masterai.pid"
test -f "${pid_file}" || { echo "MasterAI PID file is missing." >&2; exit 1; }
server_pid="$(cat "${pid_file}")"
case "${server_pid}" in *[!0-9]*|'') echo "PID file is invalid." >&2; exit 1 ;; esac
if kill -0 "${server_pid}" 2>/dev/null; then
    : >"${runtime_root}/run/stop.request"
    count=0
    while kill -0 "${server_pid}" 2>/dev/null && [ "${count}" -lt 60 ]; do
        sleep 1
        count=$((count + 1))
    done
    kill -0 "${server_pid}" 2>/dev/null &&
        { echo "MasterAI did not stop within 60 seconds." >&2; exit 1; }
fi
rm -f -- "${pid_file}"
echo "MasterAI is stopped."
