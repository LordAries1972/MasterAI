#!/usr/bin/env sh
# MasterAI systemd service unit removal.
#
# This script removes only the installed unit. Runtime data, settings, models,
# backups, service accounts, and OS-protected credentials remain untouched.
set -eu

[ "$(id -u)" -eq 0 ] ||
    { echo "The systemd uninstaller must run as root." >&2; exit 1; }
unit_path="/etc/systemd/system/masterai.service"
systemctl disable --now masterai.service 2>/dev/null || true
rm -f -- "${unit_path}"
systemctl daemon-reload
echo "Removed ${unit_path}; MasterAI data and configuration were preserved."
