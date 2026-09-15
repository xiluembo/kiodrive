# SPDX-FileCopyrightText: 2026 KIODrive authors
# SPDX-License-Identifier: LGPL-3.0-or-later
#
# Craft helper: configure and build KIODrive against D:\Craft

param(
    [string]$BuildDir = "build",
    [string]$Config = "RelWithDebInfo"
)

$ErrorActionPreference = "Stop"
. "D:\Craft\craft\craftenv.ps1"

$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
if (-not (Test-Path (Join-Path $root "CMakeLists.txt"))) {
    $root = Get-Location
}

cmake -S $root -B (Join-Path $root $BuildDir) -G Ninja "-DCMAKE_BUILD_TYPE=$Config"
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
cmake --build (Join-Path $root $BuildDir)
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
ctest --test-dir (Join-Path $root $BuildDir) --output-on-failure
exit $LASTEXITCODE
