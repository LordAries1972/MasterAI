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
    [string]$ModelsRoot = '',
    # Forces a full reconfigure and rebuild; without this, builds are incremental.
    [switch]$Clean,
    # Prints usage for every supported parameter and exits without building.
    # PowerShell parameter binding is case-insensitive, so -help/-Help/-HELP
    # all work the same as -Help.
    [switch]$Help
)

if ($Help) {
    Write-Host @'
MasterAI build script (scripts/build.ps1)

Usage:
  scripts\build.ps1 [-BuildType <Debug|Release>] [-Platform <Windows-x64>]
                     [-VerifyModels] [-ModelsRoot <path>] [-Clean] [-Help]

Parameters:
  -BuildType <Debug|Release>
      Selects the CMake build configuration. Default: Debug.

  -Platform <Windows-x64>
      Selects the target platform. Currently only Windows-x64 is supported
      (this is the only value ValidateSet accepts). Default: Windows-x64.

  -Clean
      Forces a full CMake reconfigure (--fresh) and a clean rebuild
      (--clean-first). Without this switch, builds are incremental.

  -VerifyModels
      After a successful build, runs "masterai.exe verify-models" against
      -ModelsRoot (or the project's models\ directory when -ModelsRoot is
      not given). Hashes every model file that isn't already recorded as
      verified against its manifest and writes the verification cache the
      running server trusts. Every model download is already SHA-256
      hashed and checked against its expected hash the moment it finishes
      downloading, and recorded as verified right then -- this switch is
      for models that reached the folder some other way (a manual copy, an
      import, a cache predating this check) and so were never hashed yet;
      the server itself never re-hashes a multi-gigabyte GGUF file on a
      page load or chat request.

  -ModelsRoot <path>
      Directory to verify models under when -VerifyModels is set. Defaults
      to <project root>\models when omitted.

  -Help
      Prints this usage text and exits without configuring or building
      anything.

Examples:
  scripts\build.ps1
      Incremental Debug build for Windows-x64.

  scripts\build.ps1 -BuildType Release -Clean
      Full clean Release rebuild.

  scripts\build.ps1 -VerifyModels
      Incremental Debug build, then verify every model under models\.

  scripts\build.ps1 -VerifyModels -ModelsRoot D:\models
      Incremental Debug build, then verify every model under D:\models.
'@
    exit 0
}

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
    '-S', $PSScriptRoot,
    '-B', $buildRoot,
    '-G', 'Ninja',
    "-DCMAKE_MAKE_PROGRAM:FILEPATH=$ninjaPath",
    '-DCMAKE_CXX_COMPILER:FILEPATH=cl.exe',
    "-DCMAKE_BUILD_TYPE:STRING=$BuildType"
)
if ($Clean) {
    $configureArguments = @('--fresh') + $configureArguments
}
& cmake @configureArguments
if ($LASTEXITCODE -ne 0) { throw 'CMake configuration failed.' }

$buildArguments = @('--build', $buildRoot, '--config', $BuildType, '--parallel')
if ($Clean) {
    $buildArguments += '--clean-first'
}
cmake @buildArguments
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
