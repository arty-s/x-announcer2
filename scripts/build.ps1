# Builds the plugin with the MSVC toolchain that ships with Visual Studio
# Build Tools 2022, using the CMake and Ninja bundled alongside it. Nothing
# needs to be installed or added to PATH.
#
#   powershell -ExecutionPolicy Bypass -File scripts\build.ps1
#   powershell -ExecutionPolicy Bypass -File scripts\build.ps1 -Install
#
# -Install copies the result into X-Plane's plugins folder.

param(
    [switch]$Install,
    [string]$XPlaneDir = 'E:\SteamLibrary\steamapps\common\X-Plane 12',
    [string]$Config = 'Release'
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$build = Join-Path $root 'build'

$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path $vswhere)) { throw "vswhere.exe not found - is Visual Studio Build Tools installed?" }
$vsPath = & $vswhere -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath | Select-Object -First 1
if (-not $vsPath) { throw "No Visual Studio installation with the C++ toolset was found." }

$cmake = Join-Path $vsPath 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
$ninja = Join-Path $vsPath 'Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe'
$vcvars = Join-Path $vsPath 'VC\Auxiliary\Build\vcvars64.bat'
foreach ($p in @($cmake, $ninja, $vcvars)) {
    if (-not (Test-Path $p)) { throw "Missing build tool: $p" }
}

New-Item -ItemType Directory -Force -Path $build | Out-Null

$configure = "`"$cmake`" -G Ninja -DCMAKE_MAKE_PROGRAM=`"$ninja`" -DCMAKE_BUILD_TYPE=$Config -S `"$root`" -B `"$build`""
$compile   = "`"$cmake`" --build `"$build`""

# VSCMD_SKIP_SENDTELEMETRY stops vcvars' telemetry step from shelling out to a
# vswhere it cannot find, which otherwise prints a bogus error on every build.
& cmd.exe /c "set VSCMD_SKIP_SENDTELEMETRY=1&& call `"$vcvars`" >nul && $configure && $compile"
if ($LASTEXITCODE -ne 0) { throw "Build failed with exit code $LASTEXITCODE" }

$staged = Join-Path $build 'x_announcer2'
Write-Host ""
Write-Host "Built:" -ForegroundColor Green
Get-ChildItem -Path $staged -Recurse -File | ForEach-Object {
    "  {0}  ({1:N0} bytes)" -f $_.FullName.Substring($staged.Length + 1), $_.Length
}

if ($Install) {
    $target = Join-Path $XPlaneDir 'Resources\plugins\x_announcer2'
    if (-not (Test-Path (Join-Path $XPlaneDir 'Resources\plugins'))) {
        throw "X-Plane plugins folder not found under '$XPlaneDir'"
    }
    if (Test-Path $target) { Remove-Item -Recurse -Force $target }
    Copy-Item -Recurse -Force $staged $target
    Write-Host ""
    Write-Host "Installed to $target" -ForegroundColor Green
}
