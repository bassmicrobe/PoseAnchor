[CmdletBinding()]
param(
    [ValidateSet('Debug', 'RelWithDebInfo', 'Release')]
    [string]$Configuration = 'RelWithDebInfo'
)

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$vsWhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path -LiteralPath $vsWhere)) {
    throw 'Visual Studio Installer (vswhere.exe) was not found.'
}

$vsInstall = & $vsWhere -latest -products * `
    -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
    -property installationPath
if (-not $vsInstall) {
    throw 'MSVC C++ tools are missing. Run scripts\install-build-tools.ps1 as administrator first.'
}

$cmake = Join-Path $vsInstall 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
if (-not (Test-Path -LiteralPath $cmake)) {
    throw "Visual Studio's bundled CMake was not found: $cmake"
}

Push-Location $projectRoot
try {
    & $cmake --preset vs2022-x64
    if ($LASTEXITCODE -ne 0) { throw "CMake configure failed: $LASTEXITCODE" }

    & $cmake --build (Join-Path $projectRoot 'build') --config $Configuration --parallel
    if ($LASTEXITCODE -ne 0) { throw "Build failed: $LASTEXITCODE" }

    & $cmake --build (Join-Path $projectRoot 'build') --config $Configuration --target RUN_TESTS
    if ($LASTEXITCODE -ne 0) { throw "Tests failed: $LASTEXITCODE" }
} finally {
    Pop-Location
}

$driverRoot = Join-Path $projectRoot 'build\pose_anchor'
Write-Host "Build and tests succeeded: $driverRoot"
