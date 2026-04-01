# ShaderLab Build Script
param(
    [string]$Config = "Debug",
    [switch]$Clean,
    [switch]$Run
)

Write-Host "ShaderLab Build" -ForegroundColor Cyan
Write-Host "===============" -ForegroundColor Cyan
Write-Host ""

# Clean if requested
if ($Clean -and (Test-Path "build")) {
    Write-Host "Cleaning..." -ForegroundColor Yellow
    Remove-Item "build" -Recurse -Force
}

# Find VS Build Tools
function Get-VcvarsPath {
    $vsWhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path $vsWhere) {
        $vsPath = & $vsWhere -latest -property installationPath
        if ($vsPath) {
            $candidate = Join-Path $vsPath "VC\Auxiliary\Build\vcvars64.bat"
            if (Test-Path $candidate) {
                return $candidate
            }
        }
    }

    $fallback = "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
    if (Test-Path $fallback) {
        return $fallback
    }

    return $null
}

$vcvars = Get-VcvarsPath
if (-not (Test-Path $vcvars)) {
    Write-Host "Visual Studio Build Tools not found!" -ForegroundColor Red
    exit 1
}

Write-Host "Configuring..." -ForegroundColor Yellow
$configCmd = "cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=$Config"

Write-Host "Building..." -ForegroundColor Yellow  
$buildCmd = "cmake --build build"

# Run in VS environment
$fullCmd = "`"$vcvars`" && $configCmd && $buildCmd"
cmd /c $fullCmd

if ($LASTEXITCODE -eq 0) {
    Write-Host ""
    Write-Host "Build successful!" -ForegroundColor Green
    
    # Sign binaries
    $signScript = Join-Path $PSScriptRoot "sign_build.ps1"
    if (Test-Path $signScript) {
            $binaries = @(
            "build\bin\ShaderLabIIDE.exe",
            "build\bin\ShaderLabPlayer.exe"
            )

            foreach ($binary in $binaries) {
                if (Test-Path $binary) {
                    Write-Host "Signing $binary..." -ForegroundColor Cyan
                    & $signScript -ExePath (Resolve-Path $binary)
                }
            }
    }

    $exe = "build\bin\ShaderLabIIDE.exe"
    if (Test-Path $exe) {
        if ($Run) {
            Write-Host "Running..." -ForegroundColor Cyan
            & $exe
        } else {
            Write-Host "To run: .\$exe" -ForegroundColor Cyan
        }
    }
} else {
    Write-Host ""
    Write-Host "Build failed!" -ForegroundColor Red
    exit 1
}
