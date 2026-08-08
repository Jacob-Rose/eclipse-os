# Copyright 2024 | Jake Rose
#
# This file is part of project eclipse-os
# See readme.md for full license details.
#
# Builds eclipse-dmx on Windows. Binary lands in build\Release\eclipse-dmx.exe
# (MSVC) or build\eclipse-dmx.exe (MinGW).

param(
    [string]$BuildType = "Release",
    [string]$Generator = ""
)

$ErrorActionPreference = "Stop"
Set-Location $PSScriptRoot

$configureArgs = @("-S", ".", "-B", "build", "-DCMAKE_BUILD_TYPE=$BuildType")
if ($Generator -ne "") {
    $configureArgs += @("-G", $Generator)
}

cmake @configureArgs
if ($LASTEXITCODE -ne 0) { throw "cmake configure failed" }

cmake --build build --config $BuildType
if ($LASTEXITCODE -ne 0) { throw "cmake build failed" }

$candidates = @(
    "build\$BuildType\eclipse-dmx.exe",
    "build\eclipse-dmx.exe"
)
$exe = $candidates | Where-Object { Test-Path $_ } | Select-Object -First 1

if ($exe) {
    Write-Host ""
    Write-Host "built: $(Resolve-Path $exe)"
    Write-Host "try:   $exe --config config\example.json --dry-run --frames 5"
} else {
    Write-Warning "build reported success but no eclipse-dmx.exe was found under build\"
}
