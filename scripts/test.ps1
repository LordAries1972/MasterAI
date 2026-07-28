[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')]
    [string]$BuildType = 'Debug',
    [ValidateSet('Windows-x64')]
    [string]$Platform = 'Windows-x64'
)

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$buildRoot = Join-Path $projectRoot "build\$Platform\$BuildType"

Write-Host "MasterAI test platform: $Platform"
Write-Host "MasterAI test build type: $BuildType"

ctest --test-dir $buildRoot -C $BuildType --output-on-failure
if ($LASTEXITCODE -ne 0) { throw 'MasterAI tests failed.' }
