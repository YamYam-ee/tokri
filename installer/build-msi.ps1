# Builds Tokri and packages it as a per-user MSI.
#
# Requirements:
#   - Visual Studio 2022 Build Tools (C++), CMake + Ninja
#   - Qt 6.8 for MSVC 2022 x64 with the Qt PDF module
#   - qpdf (Windows msvc64 zip from https://github.com/qpdf/qpdf/releases)
#   - WiX Toolset 5:  dotnet tool install --global wix --version 5.0.2
#                     wix extension add -g WixToolset.Util.wixext/5.0.2
#
# Usage (from a "x64 Native Tools" / vcvars64 prompt):
#   .\installer\build-msi.ps1 -Version 1.0.0
param(
    [string]$Version = "1.0.0",
    [string]$QtDir = "C:\Qt\6.8.3\msvc2022_64",
    [string]$QpdfDir = "C:\Qt\qpdf\qpdf-12.4.2-msvc64",
    [string]$RepoUrl = "https://github.com/"
)
$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
$build = Join-Path $root "build"
$stage = Join-Path $PSScriptRoot "stage"
$out = Join-Path $PSScriptRoot "Tokri-$Version.msi"

# 1. Build
cmake -S $root -B $build -G Ninja -DCMAKE_BUILD_TYPE=Release "-DCMAKE_PREFIX_PATH=$QtDir"
cmake --build $build
if ($LASTEXITCODE -ne 0) { throw "build failed" }

# 2. Stage: exe + Qt + VC++ runtime + qpdf
if (Test-Path $stage) { Remove-Item $stage -Recurse -Force }
New-Item -ItemType Directory $stage | Out-Null
Copy-Item (Join-Path $build "Tokri.exe") $stage
& (Join-Path $QtDir "bin\windeployqt.exe") --release --no-translations --no-opengl-sw `
    --no-system-d3d-compiler --no-compiler-runtime (Join-Path $stage "Tokri.exe") | Out-Null
foreach ($dll in "msvcp140.dll", "msvcp140_1.dll", "msvcp140_2.dll", "vcruntime140.dll", "vcruntime140_1.dll") {
    Copy-Item (Join-Path $env:WINDIR "System32\$dll") $stage
}
$q = New-Item -ItemType Directory (Join-Path $stage "qpdf")
Get-ChildItem (Join-Path $QpdfDir "bin") -Include "qpdf.exe", "qpdf*.dll", "*140*.dll" -Recurse |
    Copy-Item -Destination $q
Get-ChildItem $QpdfDir -Recurse -Include "license*" -Depth 3 | Copy-Item -Destination $q
Copy-Item (Join-Path $root "LICENSE.md") $stage

# 3. MSI
wix build (Join-Path $PSScriptRoot "Tokri.wxs") -arch x64 -ext WixToolset.Util.wixext `
    -d "Version=$Version" -d "RepoUrl=$RepoUrl" `
    -d "IconFile=$(Join-Path $root 'resources\com.ysuraj.Tokri.ico')" `
    -bindpath "app=$stage" `
    -o $out
if ($LASTEXITCODE -ne 0) { throw "wix build failed" }
Write-Host "Built $out"
