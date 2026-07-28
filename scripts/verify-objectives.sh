#!/usr/bin/env sh
set -eu

project_root="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
objectives="${project_root}/docs/objectives.md"
cache="${project_root}/docs/objectives.sha256"

if [ ! -f "${objectives}" ] || [ ! -f "${cache}" ]; then
    echo "MasterAI objectives or cached hash is missing." >&2
    exit 2
fi

expected="$(awk 'NR == 1 { print $1 }' "${cache}")"
actual="$(sha256sum "${objectives}" | awk '{ print toupper($1) }')"

if [ "$(printf '%s' "${expected}" | tr '[:lower:]' '[:upper:]')" != "${actual}" ]; then
    echo "Project objectives changed. Reassess docs/objectives.md before updating the cached hash." >&2
    exit 3
fi

printf 'MasterAI objectives verified: %s\n' "${actual}"
