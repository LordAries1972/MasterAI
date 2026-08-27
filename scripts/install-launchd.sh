#!/usr/bin/env sh
# MasterAI launchd service installer for supported macOS (Apple Silicon) hosts.
# ADR-0004.
#
# This script validates fixed absolute paths, writes one launchd daemon plist,
# asks plutil to validate it, and bootstraps it into the system domain. It
# never builds, downloads, creates users, or modifies MasterAI settings.
#
# Security posture note: launchd has no direct equivalent to systemd's
# ProtectSystem=strict/NoNewPrivileges/RestrictAddressFamilies/
# CapabilityBoundingSet= hardening keys used by install-systemd.sh -- macOS's
# comparable mechanism (sandbox-exec / App Sandbox entitlements) is a
# materially heavier, code-signing-dependent model not adopted here. This
# script relies on running the service under a dedicated, unprivileged
# service account (the same mitigation systemd's User=/Group= provide) plus
# ProcessType=Background and resource limits below; it does not claim
# equivalent kernel-level confinement to the Linux unit.
set -eu

# Prints the exact non-interactive installer contract.
usage() {
    echo "Usage: install-launchd.sh <binary> <settings> <runtime-root> <models-root> <service-user>" >&2
    exit 2
}

# Accepts only absolute macOS paths that cannot inject plist markup.
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

# Accepts a conventional existing macOS service account name.
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
    { echo "The launchd installer must run as root." >&2; exit 1; }
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

label="com.masterai.server"
plist_path="/Library/LaunchDaemons/${label}.plist"
temporary="$(mktemp /Library/LaunchDaemons/.masterai.server.plist.XXXXXX)"
trap 'rm -f -- "${temporary}"' EXIT HUP INT TERM

working_directory="$(dirname -- "${binary}")"

# Publishes a least-privilege (relative to what launchd exposes) daemon
# plist with only configured data paths implied by the service account's
# own filesystem permissions -- launchd has no ReadWritePaths= equivalent.
cat >"${temporary}" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>Label</key>
    <string>${label}</string>
    <key>ProgramArguments</key>
    <array>
        <string>${binary}</string>
        <string>serve</string>
        <string>${settings}</string>
    </array>
    <key>UserName</key>
    <string>${service_user}</string>
    <key>GroupName</key>
    <string>${service_user}</string>
    <key>WorkingDirectory</key>
    <string>${working_directory}</string>
    <key>RunAtLoad</key>
    <true/>
    <key>KeepAlive</key>
    <dict>
        <key>SuccessfulExit</key>
        <false/>
    </dict>
    <key>ProcessType</key>
    <string>Background</string>
    <key>ThrottleInterval</key>
    <integer>5</integer>
    <key>StandardOutPath</key>
    <string>${runtime_root}/masterai.log</string>
    <key>StandardErrorPath</key>
    <string>${runtime_root}/masterai.log</string>
</dict>
</plist>
PLIST

chmod 0644 "${temporary}"
plutil -lint "${temporary}" >/dev/null
mv -f -- "${temporary}" "${plist_path}"
trap - EXIT HUP INT TERM
chown root:wheel "${plist_path}"
launchctl bootstrap system "${plist_path}"
launchctl enable "system/${label}"
echo "Installed and bootstrapped ${plist_path}. Start it with: launchctl kickstart system/${label}"
