# Copyright 2024 | Jake Rose
#
# This file is part of project eclipse-os
# See readme.md for full license details.
#
# Builds eclipse-dmx on Windows.
#
#   .\tools\setup-toolchain.ps1    (once per machine)
#   .\build.ps1
#
# Uses the toolchain recorded by setup-toolchain.ps1 when there is one, so the
# build does not depend on what happens to be on PATH in a given shell.

param(
    [string]$BuildType = "Release",
    [string]$Generator = "",
    [switch]$Clean
)

$ErrorActionPreference = "Stop"
Set-Location $PSScriptRoot

# ---- toolchain ------------------------------------------------------------
$envFile = Join-Path $PSScriptRoot "tools\toolchain.env.ps1"
if (Test-Path $envFile) {
    . $envFile
}

$mingwBin = $env:ECLIPSE_DMX_MINGW_BIN
if ($mingwBin -and (Test-Path $mingwBin)) {
    if ($env:PATH -notlike "*$mingwBin*") {
        $env:PATH = "$mingwBin;$env:PATH"
    }
}

if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
    throw "cmake is not available. Run .\tools\setup-toolchain.ps1 first."
}

# ---- generator ------------------------------------------------------------
# With MinGW, CMake's default is the Visual Studio generator, which will not
# find gcc. Pick explicitly rather than letting it guess wrong.
if ($Generator -eq "") {
    if ($mingwBin) {
        $Generator = if (Get-Command ninja -ErrorAction SilentlyContinue) { "Ninja" } else { "MinGW Makefiles" }
    }
}

if ($Clean -and (Test-Path "build")) {
    Write-Host "removing build\"
    Remove-Item -Recurse -Force "build"
}

$configureArgs = @("-S", ".", "-B", "build", "-DCMAKE_BUILD_TYPE=$BuildType")

if ($Generator -ne "") {
    $configureArgs += @("-G", $Generator)
}

if ($mingwBin) {
    # Name the compiler outright so a stray cl.exe on PATH cannot win.
    $configureArgs += "-DCMAKE_CXX_COMPILER=$($mingwBin -replace '\\','/')/g++.exe"
}

Write-Host "configuring ($Generator $BuildType)..."
cmake @configureArgs
if ($LASTEXITCODE -ne 0) { throw "cmake configure failed" }

Write-Host "building..."
cmake --build build --config $BuildType
if ($LASTEXITCODE -ne 0) { throw "cmake build failed" }

$candidates = @(
    "build\eclipse-dmx.exe",
    "build\$BuildType\eclipse-dmx.exe"
)
$exe = $candidates | Where-Object { Test-Path $_ } | Select-Object -First 1

if (-not $exe) {
    Write-Warning "build reported success but no eclipse-dmx.exe was found under build\"
    exit 1
}

Write-Host ""
Write-Host "built: $(Resolve-Path $exe)"
Write-Host "try:   $exe --config config\example.json --dry-run --frames 5"
