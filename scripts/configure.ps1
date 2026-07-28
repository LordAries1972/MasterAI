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
if (-not (Test-Path -LiteralPath $binary -PathType Leaf)) {
    throw "MasterAI binary is missing: $binary"
}
& $binary configure $Settings
if ($LASTEXITCODE -ne 0) { throw 'MasterAI configuration failed.' }
