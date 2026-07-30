[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')]
    [string]$BuildType = 'Debug',
    [ValidateSet('Windows-x64')]
    [string]$Platform = 'Windows-x64',
    # Hashes every model file under models-root against its manifest and
    # writes the verification cache the running server trusts, so new or
    # changed models stop reporting as "unverified" in the UI. This is the
    # only place model files get hashed -- the server itself never re-hashes
    # a multi-gigabyte GGUF file on a page load or chat request.
    [switch]$VerifyModels,
    [string]$ModelsRoot = ''
)

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$buildRoot = Join-Path $projectRoot "build\$Platform\$BuildType"

Write-Host "MasterAI platform: $Platform"
Write-Host "MasterAI build type: $BuildType"

$vswhereDirectory =
    'C:\Program Files (x86)\Microsoft Visual Studio\Installer'
if (Test-Path -LiteralPath (Join-Path $vswhereDirectory 'vswhere.exe') -PathType Leaf) {
    $env:Path = "$vswhereDirectory;$env:Path"
}

if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue)) {
    $developerCommand = $null
    $installationRoots = @(
        'C:\Program Files\Microsoft Visual Studio\2022',
        'D:\Program Files\Microsoft Visual Studio\2022'
    )
    $editions = @('Enterprise', 'Professional', 'Community', 'BuildTools')
    foreach ($installationRoot in $installationRoots) {
        foreach ($edition in $editions) {
            $candidate =
                Join-Path $installationRoot "$edition\Common7\Tools\VsDevCmd.bat"
            if (Test-Path -LiteralPath $candidate -PathType Leaf) {
                $developerCommand = $candidate
                break
            }
        }
        if ($developerCommand) { break }
    }
    if (-not $developerCommand) {
        throw 'MSVC developer environment was not found on C: or D:.'
    }

    $environmentLines = & $env:ComSpec /d /s /c (
        "`"$developerCommand`" -no_logo -arch=x64 -host_arch=x64 >nul && set"
    )
    if ($LASTEXITCODE -ne 0) {
        throw 'MSVC developer environment initialization failed.'
    }
    foreach ($line in $environmentLines) {
        $separator = $line.IndexOf('=')
        if ($separator -gt 0) {
            $name = $line.Substring(0, $separator)
            $value = $line.Substring($separator + 1)
            [Environment]::SetEnvironmentVariable($name, $value, 'Process')
        }
    }
}

$ninjaPath =
    'D:\Program Files\Microsoft Visual Studio\2022\Enterprise\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe'
if (-not (Test-Path -LiteralPath $ninjaPath -PathType Leaf)) {
    throw "Visual Studio Ninja executable is missing: $ninjaPath"
}

$configureArguments = @(
    '--fresh',
    '-S', $PSScriptRoot,
    '-B', $buildRoot,
    '-G', 'Ninja',
    "-DCMAKE_MAKE_PROGRAM:FILEPATH=$ninjaPath",
    '-DCMAKE_CXX_COMPILER:FILEPATH=cl.exe',
    "-DCMAKE_BUILD_TYPE:STRING=$BuildType"
)
& cmake @configureArguments
if ($LASTEXITCODE -ne 0) { throw 'CMake configuration failed.' }

cmake --build $buildRoot --config $BuildType --parallel
if ($LASTEXITCODE -ne 0) { throw 'MasterAI build failed.' }

if ($VerifyModels) {
    if (-not $ModelsRoot) { $ModelsRoot = Join-Path $projectRoot 'models' }
    $binary = Join-Path $buildRoot 'masterai.exe'
    Write-Host "Verifying models under $ModelsRoot ..."
    & $binary verify-models $ModelsRoot
    if ($LASTEXITCODE -ne 0) {
        throw 'MasterAI model verification reported one or more failures.'
    }
}
