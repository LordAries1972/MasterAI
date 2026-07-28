#!/usr/bin/env sh
set -eu
project_root="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
settings="${1:-${project_root}/config/settings.json}"
build_type="${2:-Release}"
binary="${project_root}/build/Linux-x86_64/${build_type}/masterai"
test -x "${binary}" || { echo "MasterAI binary is missing: ${binary}" >&2; exit 1; }
exec "${binary}" configure "${settings}"
