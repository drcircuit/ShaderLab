# ShaderLab Environment Setup Script
Write-Host "ShaderLab Environment Setup" -ForegroundColor Cyan
Write-Host "=============================" -ForegroundColor Cyan
Write-Host ""

# Function to check if a command exists
function Test-Command {
    param([string]$command)
    $null -ne (Get-Command $command -ErrorAction SilentlyContinue)
}

# Check for Visual Studio
Write-Host "Checking for Visual Studio..." -ForegroundColor Yellow
$vsWhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (Test-Path $vsWhere) {
    $vsPath = & $vsWhere -latest -property installationPath
    if ($vsPath) {
        Write-Host "  Found: $vsPath" -ForegroundColor Green
    }
} else {
    Write-Host "  Not found - Download from: https://visualstudio.microsoft.com/downloads/" -ForegroundColor Red
}

# Check for CMake
Write-Host "Checking for CMake..." -ForegroundColor Yellow
if (Test-Command cmake) {
    $cmakeVersion = cmake --version | Select-Object -First 1
    Write-Host "  Found: $cmakeVersion" -ForegroundColor Green
} else {
    Write-Host "  Not found - Download from: https://cmake.org/download/" -ForegroundColor Red
}

Write-Host ""
Write-Host "Downloading third-party dependencies..." -ForegroundColor Yellow
Write-Host ""

$thirdPartyDir = Join-Path $PSScriptRoot "..\third_party"

# Create directories
$dirs = @(
    (Join-Path $thirdPartyDir "imgui"),
    (Join-Path $thirdPartyDir "miniaudio"),
    (Join-Path $thirdPartyDir "json\include\nlohmann"),
    (Join-Path $thirdPartyDir "stb")
)

foreach ($dir in $dirs) {
    if (-not (Test-Path $dir)) {
        New-Item -ItemType Directory -Path $dir -Force | Out-Null
    }
}

# Download miniaudio
Write-Host "miniaudio:" -ForegroundColor Cyan
$miniAudioPath = Join-Path $thirdPartyDir "miniaudio\miniaudio.h"
if (-not (Test-Path $miniAudioPath)) {
    try {
        Write-Host "  Downloading..." -NoNewline
        [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
        Invoke-WebRequest -Uri "https://raw.githubusercontent.com/mackron/miniaudio/master/miniaudio.h" -OutFile $miniAudioPath -UseBasicParsing
        Write-Host " Done" -ForegroundColor Green
    } catch {
        Write-Host " Failed" -ForegroundColor Red
    }
} else {
    Write-Host "  Already exists" -ForegroundColor Green
}

# Download nlohmann/json
Write-Host "nlohmann/json:" -ForegroundColor Cyan
$jsonPath = Join-Path $thirdPartyDir "json\include\nlohmann\json.hpp"
if (-not (Test-Path $jsonPath)) {
    try {
        Write-Host "  Downloading..." -NoNewline
        Invoke-WebRequest -Uri "https://raw.githubusercontent.com/nlohmann/json/develop/single_include/nlohmann/json.hpp" -OutFile $jsonPath -UseBasicParsing
        Write-Host " Done" -ForegroundColor Green
    } catch {
        Write-Host " Failed" -ForegroundColor Red
    }
} else {
    Write-Host "  Already exists" -ForegroundColor Green
}

# Download stb_image
Write-Host "stb_image:" -ForegroundColor Cyan
$stbPath = Join-Path $thirdPartyDir "stb\stb_image.h"
if (-not (Test-Path $stbPath)) {
    try {
        Write-Host "  Downloading..." -NoNewline
        Invoke-WebRequest -Uri "https://raw.githubusercontent.com/nothings/stb/master/stb_image.h" -OutFile $stbPath -UseBasicParsing
        Write-Host " Done" -ForegroundColor Green
    } catch {
        Write-Host " Failed" -ForegroundColor Red
    }
} else {
    Write-Host "  Already exists" -ForegroundColor Green
}

# Dear ImGui
Write-Host "Dear ImGui:" -ForegroundColor Cyan
$imguiPath = Join-Path $thirdPartyDir "imgui\imgui.h"
if (-not (Test-Path $imguiPath)) {
    Write-Host "  Manual download required:" -ForegroundColor Yellow
    Write-Host "    1. Download: https://github.com/ocornut/imgui/archive/refs/heads/docking.zip" -ForegroundColor White
    Write-Host "    2. Extract all files to: third_party\imgui\" -ForegroundColor White
} else {
    Write-Host "  Already exists" -ForegroundColor Green
}

Write-Host ""
Write-Host "Setup complete!" -ForegroundColor Green
Write-Host ""
Write-Host "Next: .\tools\build.ps1" -ForegroundColor Cyan
