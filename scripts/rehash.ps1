# Verifies every model file's SHA-256 against its manifest and writes the
# verification cache the running server trusts (models_root/.verified-cache.json).
# This is the only place model files get hashed -- ModelRegistry::scan(), used by
# every inventory/chat/download page load and API call, only ever reads that
# cache, so it never blocks a request on gigabytes of hashing. Run this after
# adding, replacing, or removing files under models-root.
[CmdletBinding()]
param(
    [string]$Settings = '',
    [ValidateSet('Debug', 'Release')]
    [string]$BuildType = 'Release',
    [string]$ModelsRoot = ''
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
if (-not $Settings) { $Settings = Join-Path $projectRoot 'config\settings.json' }
$binary = Join-Path $projectRoot "build\Windows-x64\$BuildType\masterai.exe"
if (-not (Test-Path -LiteralPath $binary -PathType Leaf)) {
    throw "MasterAI binary not found: $binary (build it first with scripts\build.ps1)"
}

if (-not $ModelsRoot) {
    if (-not (Test-Path -LiteralPath $Settings -PathType Leaf)) {
        throw "MasterAI settings file not found: $Settings"
    }
    $config = Get-Content -LiteralPath $Settings -Raw | ConvertFrom-Json
    $ModelsRoot = $config.workspace.modelsRoot
    if (-not $ModelsRoot) {
        throw 'workspace.modelsRoot is missing from settings.'
    }
    if (-not [IO.Path]::IsPathRooted($ModelsRoot)) {
        # Settings store this relative to the project root, the same way
        # start/stop resolve runtimeRoot.
        $ModelsRoot = [IO.Path]::GetFullPath((Join-Path $projectRoot $ModelsRoot))
    }
}
if (-not (Test-Path -LiteralPath $ModelsRoot -PathType Container)) {
    throw "Models root not found: $ModelsRoot"
}

Write-Host "Verifying models under $ModelsRoot ..."
Write-Host '(large model files are hashed in full -- this can take a while)'
$stopwatch = [Diagnostics.Stopwatch]::StartNew()

# Streams masterai.exe's own per-model progress lines ("[n] id ... OK" /
# "... FAILED: reason") straight through as they arrive, instead of buffering
# until the whole verify-models run completes.
& $binary verify-models $ModelsRoot | ForEach-Object { Write-Host $_ }
$exitCode = $LASTEXITCODE

$stopwatch.Stop()
Write-Host ("Finished in {0:N1}s." -f $stopwatch.Elapsed.TotalSeconds)
if ($exitCode -ne 0) {
    throw 'Model verification reported one or more failures -- see FAILED lines above.'
}
Write-Host 'All discovered models verified. New/changed models will now show as available.'
