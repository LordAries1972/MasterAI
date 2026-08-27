# Debug-build convenience wrapper around stop.ps1: always targets
# build\Windows-x64\Debug\masterai.exe instead of Release, matching
# start_debug.ps1.
[CmdletBinding()]
param(
    [string]$Settings = ''
)
$ErrorActionPreference = 'Stop'
& (Join-Path $PSScriptRoot 'stop.ps1') -BuildType Debug -Settings $Settings
