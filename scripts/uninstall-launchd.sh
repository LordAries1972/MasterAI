#!/usr/bin/env sh
# MasterAI launchd daemon plist removal. ADR-0004.
#
# This script removes only the installed plist. Runtime data, settings, models,
# backups, service accounts, and OS-protected credentials remain untouched.
set -eu

[ "$(id -u)" -eq 0 ] ||
    { echo "The launchd uninstaller must run as root." >&2; exit 1; }
label="com.masterai.server"
plist_path="/Library/LaunchDaemons/${label}.plist"
launchctl bootout "system/${label}" 2>/dev/null || true
rm -f -- "${plist_path}"
echo "Removed ${plist_path}; MasterAI data and configuration were preserved."
