[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$objectivesPath = Join-Path $projectRoot 'docs\objectives.md'
$cachePath = Join-Path $projectRoot 'docs\objectives.sha256'

if (-not (Test-Path -LiteralPath $objectivesPath -PathType Leaf)) {
    throw 'docs/objectives.md is missing.'
}
if (-not (Test-Path -LiteralPath $cachePath -PathType Leaf)) {
    throw 'docs/objectives.sha256 is missing.'
}

$expected = ((Get-Content -LiteralPath $cachePath -Raw).Trim() -split '\s+')[0]
$actual = (Get-FileHash -Algorithm SHA256 -LiteralPath $objectivesPath).Hash

if (-not $actual.Equals($expected, [System.StringComparison]::OrdinalIgnoreCase)) {
    Write-Error "Project objectives changed. Expected $expected but found $actual. Reassess docs/objectives.md before updating the cached hash."
    exit 3
}

Write-Host "MasterAI objectives verified: $actual"
