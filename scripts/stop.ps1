# Requests graceful MasterAI shutdown through the configured runtime control
# file and waits for the recorded process to exit.
[CmdletBinding()]
param(
    [string]$Settings = '',
    [ValidateSet('Debug', 'Release')]
    [string]$BuildType = 'Release'
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
if (-not $Settings) { $Settings = Join-Path $projectRoot 'config\settings.json' }
$binary = Join-Path $projectRoot "build\Windows-x64\$BuildType\masterai.exe"
# Resolve the same native runtime root used by start and server operations.
$runtimeRoot = (& $binary runtime-root $Settings).Trim()
if ($LASTEXITCODE -ne 0 -or -not $runtimeRoot) {
    throw 'MasterAI runtime root could not be resolved from settings.'
}
if (-not [IO.Path]::IsPathRooted($runtimeRoot)) {
    $runtimeRoot = [IO.Path]::GetFullPath($runtimeRoot, $projectRoot)
}
$pidFile = Join-Path $runtimeRoot 'run\masterai.pid'
$stopFile = Join-Path $runtimeRoot 'run\stop.request'
if (-not (Test-Path -LiteralPath $pidFile -PathType Leaf)) {
    throw 'MasterAI PID file is missing.'
}
$masterAiPid = [int](Get-Content -LiteralPath $pidFile -Raw)
$process = Get-Process -Id $masterAiPid -ErrorAction SilentlyContinue
if ($process) {
    Set-Content -LiteralPath $stopFile -Value 'stop' -NoNewline
    if (-not $process.WaitForExit(30000)) {
        throw 'MasterAI did not complete graceful shutdown within 30 seconds.'
    }
}
Remove-Item -LiteralPath $pidFile
Remove-Item -LiteralPath $stopFile -ErrorAction SilentlyContinue
Write-Host 'MasterAI is stopped.'
