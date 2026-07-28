# Runs MasterAI security and hardware diagnostics against the runtime selected
# by the validated settings file.
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
# Reuse native configuration parsing rather than maintaining a script parser.
$runtimeRoot = (& $binary runtime-root $Settings).Trim()
if ($LASTEXITCODE -ne 0 -or -not $runtimeRoot) {
    throw 'MasterAI runtime root could not be resolved from settings.'
}
if (-not [IO.Path]::IsPathRooted($runtimeRoot)) {
    $runtimeRoot = [IO.Path]::GetFullPath($runtimeRoot, $projectRoot)
}
& $binary security-status $runtimeRoot
$securityExit = $LASTEXITCODE
& $binary probe $projectRoot
$probeExit = $LASTEXITCODE
if ($securityExit -ne 0 -or $probeExit -ne 0) {
    throw 'One or more MasterAI diagnostics failed.'
}
