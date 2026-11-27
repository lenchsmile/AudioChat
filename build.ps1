#!/usr/bin/env pwsh

# Build script for AudioChat project
# Usage: .\build.ps1 [-Config <Debug|Release>] [-Toolchain <Clang|MSVC>]

param(
    [ValidateSet("Debug", "Release")]
    [string]$Config = "Debug",
    [ValidateSet("Clang", "MSVC")]
    [string]$Toolchain = "Clang"
)

$ErrorActionPreference = "Stop"

# Determine preset name
$Preset = "$($Toolchain.ToLower())-$($Config.ToLower())"

Write-Host "========================================" -ForegroundColor Cyan
Write-Host "Building AudioChat" -ForegroundColor Cyan
Write-Host "Config: $Config" -ForegroundColor Cyan
Write-Host "Toolchain: $Toolchain" -ForegroundColor Cyan
Write-Host "Preset: $Preset" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan

# Check if preset exists
$presetFile = Get-Content "CMakePresets.json" -Raw | ConvertFrom-Json
if (-not ($presetFile.configurePresets.name -contains $Preset)) {
    Write-Host "Error: Preset '$Preset' not found" -ForegroundColor Red
    exit 1
}

# Configure
Write-Host "`n[1/2] Configuring..." -ForegroundColor Yellow
$ConfigureArgs = @('--preset', $Preset)
if ($env:VCPKG_ROOT) {
    $ConfigureArgs += "-DCMAKE_TOOLCHAIN_FILE=$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
}
cmake @ConfigureArgs
if ($LASTEXITCODE -ne 0) {
    Write-Host "Configuration failed!" -ForegroundColor Red
    exit 1
}

# Build
Write-Host "`n[2/2] Building..." -ForegroundColor Yellow
cmake --build --preset $Preset
if ($LASTEXITCODE -ne 0) {
    Write-Host "Build failed!" -ForegroundColor Red
    exit 1
}

Write-Host "`nBuild completed successfully!" -ForegroundColor Green
Write-Host "Executable: out/build/$Preset/bin/AudioChat.exe" -ForegroundColor Green