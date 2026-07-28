#!/usr/bin/env sh
# MasterAI systemd service installer for supported Linux release hosts.
#
# This script validates fixed absolute paths, writes one hardened native service
# unit, asks systemd to validate it, and enables it. It never builds, downloads,
# creates users, or modifies MasterAI settings.
set -eu

# Prints the exact non-interactive installer contract.
usage() {
    echo "Usage: install-systemd.sh <binary> <settings> <runtime-root> <models-root> <service-user>" >&2
    exit 2
}

# Accepts only absolute Linux paths that cannot inject systemd directives.
require_safe_path() {
    value="$1"
    label="$2"
    case "${value}" in
        /*) ;;
        *) echo "${label} must be an absolute path." >&2; exit 2 ;;
    esac
    case "${value}" in
        *[!A-Za-z0-9_./-]*)
            echo "${label} contains an unsupported character." >&2
            exit 2
            ;;
    esac
}

# Accepts a conventional existing Linux service account name.
require_safe_user() {
    case "$1" in
        ''|*[!A-Za-z0-9_-]*)
            echo "Service user is invalid." >&2
            exit 2
            ;;
    esac
}

[ "$#" -eq 5 ] || usage
[ "$(id -u)" -eq 0 ] ||
    { echo "The systemd installer must run as root." >&2; exit 1; }
binary="$1"
settings="$2"
runtime_root="$3"
models_root="$4"
service_user="$5"
require_safe_path "${binary}" "Binary"
require_safe_path "${settings}" "Settings"
require_safe_path "${runtime_root}" "Runtime root"
require_safe_path "${models_root}" "Models root"
require_safe_user "${service_user}"
[ -x "${binary}" ] || { echo "MasterAI binary is not executable." >&2; exit 1; }
[ -f "${settings}" ] || { echo "MasterAI settings file is missing." >&2; exit 1; }
id "${service_user}" >/dev/null 2>&1 ||
    { echo "MasterAI service user does not exist." >&2; exit 1; }

unit_path="/etc/systemd/system/masterai.service"
temporary="$(mktemp /etc/systemd/system/.masterai.service.XXXXXX)"
trap 'rm -f -- "${temporary}"' EXIT HUP INT TERM

# Publishes a least-privilege unit with only configured data paths writable.
{
    echo "[Unit]"
    echo "Description=MasterAI local programming AI server"
    echo "After=network.target"
    echo "StartLimitIntervalSec=120"
    echo "StartLimitBurst=3"
    echo
    echo "[Service]"
    echo "Type=simple"
    echo "User=${service_user}"
    echo "Group=${service_user}"
    echo "ExecStart=${binary} serve ${settings}"
    echo "WorkingDirectory=$(dirname -- "${binary}")"
    echo "Restart=on-failure"
    echo "RestartSec=5"
    echo "TimeoutStopSec=45"
    echo "KillSignal=SIGTERM"
    echo "UMask=0077"
    echo "NoNewPrivileges=yes"
    echo "PrivateTmp=yes"
    echo "ProtectSystem=strict"
    echo "ProtectHome=read-only"
    echo "ProtectControlGroups=yes"
    echo "ProtectKernelModules=yes"
    echo "ProtectKernelTunables=yes"
    echo "ProtectKernelLogs=yes"
    echo "ProtectClock=yes"
    echo "ProtectHostname=yes"
    echo "LockPersonality=yes"
    echo "RestrictSUIDSGID=yes"
    echo "RestrictRealtime=yes"
    echo "SystemCallArchitectures=native"
    echo "RestrictAddressFamilies=AF_INET AF_UNIX"
    echo "CapabilityBoundingSet="
    echo "AmbientCapabilities="
    echo "ReadWritePaths=${runtime_root} ${models_root}"
    echo
    echo "[Install]"
    echo "WantedBy=multi-user.target"
} >"${temporary}"

chmod 0644 "${temporary}"
systemd-analyze verify "${temporary}"
mv -f -- "${temporary}" "${unit_path}"
trap - EXIT HUP INT TERM
systemctl daemon-reload
systemctl enable masterai.service
echo "Installed and enabled ${unit_path}. Start it with: systemctl start masterai"
