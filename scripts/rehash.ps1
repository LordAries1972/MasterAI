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
    # Asks the binary to resolve workspace.modelsRoot instead of parsing the
    # JSON and resolving the relative path ourselves: a relative modelsRoot
    # resolves against the directory holding settings.json (see
    # ConfigurationManager::load's resolve_workspace_path in config.cpp), not
    # against the project root -- resolving it here against $projectRoot
    # previously pointed at the wrong directory whenever settings.json lived
    # somewhere other than "<project root>/config". start.ps1/stop.ps1 already
    # get runtimeRoot the same way, via the runtime-root command.
    $ModelsRoot = (& $binary models-root $Settings).Trim()
    if ($LASTEXITCODE -ne 0 -or -not $ModelsRoot) {
        throw "Failed to resolve workspace.modelsRoot from $Settings"
    }
    if (-not [IO.Path]::IsPathRooted($ModelsRoot)) {
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
