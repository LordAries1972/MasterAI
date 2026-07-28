# Removes only MasterAI's generated build tree; source, models, and runtime data
# remain untouched. Use -WhatIf to preview the operation without deleting files.
[CmdletBinding(SupportsShouldProcess, ConfirmImpact = 'Medium')]
param()

$ErrorActionPreference = 'Stop'
$projectRoot = [IO.Path]::GetFullPath((Split-Path -Parent $PSScriptRoot))
$buildRoot = [IO.Path]::GetFullPath((Join-Path $projectRoot 'build'))
$expectedBuildRoot = [IO.Path]::GetFullPath(
    [IO.Path]::Combine($projectRoot, 'build')
)

if ($buildRoot -ne $expectedBuildRoot -or
    [IO.Path]::GetFileName($buildRoot) -ne 'build') {
    throw "Refusing to clean an unexpected path: $buildRoot"
}

if (-not (Test-Path -LiteralPath $buildRoot)) {
    Write-Host "MasterAI build tree is already clean: $buildRoot"
    return
}

if ($PSCmdlet.ShouldProcess($buildRoot, 'Remove MasterAI generated build tree')) {
    Remove-Item -LiteralPath $buildRoot -Recurse -Force
    Write-Host "Removed MasterAI build tree: $buildRoot"
}
