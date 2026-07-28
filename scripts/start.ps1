# Starts MasterAI from validated settings and records its PID under the
# authoritative configured runtime root.
[CmdletBinding()]
param(
    [string]$Settings = '',
    [ValidateSet('Debug', 'Release')]
    [string]$BuildType = 'Release',
    [switch]$Foreground
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
if (-not $Settings) { $Settings = Join-Path $projectRoot 'config\settings.json' }
$binary = Join-Path $projectRoot "build\Windows-x64\$BuildType\masterai.exe"
if (-not (Test-Path -LiteralPath $binary -PathType Leaf)) {
    throw "MasterAI binary is missing: $binary"
}
if (-not (Test-Path -LiteralPath $Settings -PathType Leaf)) {
    if (-not [Environment]::UserInteractive) {
        throw 'Configuration is missing and non-interactive startup cannot run the wizard.'
    }
    & $binary configure $Settings
    if ($LASTEXITCODE -ne 0) { throw 'MasterAI configuration failed.' }
}
if ($Foreground) {
    & $binary serve $Settings
    exit $LASTEXITCODE
}
# Ask the native configuration parser for the runtime root to avoid duplicating
# JSON/configuration precedence rules in PowerShell.
$runtimeRoot = (& $binary runtime-root $Settings).Trim()
if ($LASTEXITCODE -ne 0 -or -not $runtimeRoot) {
    throw 'MasterAI runtime root could not be resolved from settings.'
}
if (-not [IO.Path]::IsPathRooted($runtimeRoot)) {
    $runtimeRoot = [IO.Path]::GetFullPath($runtimeRoot, $projectRoot)
}
$runRoot = Join-Path $runtimeRoot 'run'
New-Item -ItemType Directory -Path $runRoot -Force | Out-Null
$process = Start-Process -FilePath $binary -ArgumentList @('serve', $Settings) `
    -PassThru -WindowStyle Hidden -WorkingDirectory $projectRoot
Set-Content -LiteralPath (Join-Path $runRoot 'masterai.pid') `
    -Value $process.Id -NoNewline
Write-Host "MasterAI started with process ID $($process.Id)."
