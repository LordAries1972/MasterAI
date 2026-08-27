# Debug-build convenience wrapper around start.ps1: always launches
# build\Windows-x64\Debug\masterai.exe instead of Release, and -- unlike
# start.ps1's own Start-Process call, which leaves stdout/stderr
# unredirected under -WindowStyle Hidden -- captures both streams to a log
# file under the runtime root, since this server logs every startup event
# (including a fatal early failure) to stderr only, with nothing else to
# read if that isn't captured somewhere.
[CmdletBinding()]
param(
    [string]$Settings = ''
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
if (-not $Settings) { $Settings = Join-Path $projectRoot 'config\settings.json' }
$binary = Join-Path $projectRoot 'build\Windows-x64\Debug\masterai.exe'
if (-not (Test-Path -LiteralPath $binary -PathType Leaf)) {
    throw "MasterAI Debug binary is missing: $binary (build it first with scripts\build.ps1 -BuildType Debug)"
}
if (-not (Test-Path -LiteralPath $Settings -PathType Leaf)) {
    if (-not [Environment]::UserInteractive) {
        throw 'Configuration is missing and non-interactive startup cannot run the wizard.'
    }
    & $binary configure $Settings
    if ($LASTEXITCODE -ne 0) { throw 'MasterAI configuration failed.' }
}
$runtimeRoot = (& $binary runtime-root $Settings).Trim()
if ($LASTEXITCODE -ne 0 -or -not $runtimeRoot) {
    throw 'MasterAI runtime root could not be resolved from settings.'
}
if (-not [IO.Path]::IsPathRooted($runtimeRoot)) {
    $runtimeRoot = [IO.Path]::GetFullPath((Join-Path $projectRoot $runtimeRoot))
}
$runRoot = Join-Path $runtimeRoot 'run'
New-Item -ItemType Directory -Path $runRoot -Force | Out-Null
$stdoutLog = Join-Path $runRoot 'masterai-debug.stdout.log'
$stderrLog = Join-Path $runRoot 'masterai-debug.stderr.log'
$process = Start-Process -FilePath $binary -ArgumentList @('serve', $Settings) `
    -PassThru -WindowStyle Hidden -WorkingDirectory $projectRoot `
    -RedirectStandardOutput $stdoutLog -RedirectStandardError $stderrLog
Set-Content -LiteralPath (Join-Path $runRoot 'masterai.pid') `
    -Value $process.Id -NoNewline
Write-Host "MasterAI (Debug) started with process ID $($process.Id)."
Write-Host "Logs: $stderrLog"
