param(
    [string]$ProjectPath = "",
    [string]$OutputRoot = "",
    [int]$RegularMaxBytes = 262144
)

$ErrorActionPreference = "Stop"

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$cliPath = Join-Path $repoRoot "build\bin\ShaderLabBuildCli.exe"

if ([string]::IsNullOrWhiteSpace($ProjectPath)) {
    $candidates = @(
        (Join-Path $repoRoot "DEMO.json"),
        (Join-Path (Split-Path $repoRoot -Parent) "DEMO.json")
    )
    foreach ($candidate in $candidates) {
        if (Test-Path $candidate) {
            $ProjectPath = (Resolve-Path $candidate).Path
            break
        }
    }
}

if ([string]::IsNullOrWhiteSpace($OutputRoot)) {
    $OutputRoot = Join-Path $repoRoot "build_integration"
}

if (-not (Test-Path $ProjectPath)) {
    throw "Project JSON not found: $ProjectPath. Pass -ProjectPath or add DEMO.json in repo root/parent."
}

if (-not (Test-Path $cliPath)) {
    throw "ShaderLabBuildCli not found at $cliPath. Build the Debug workspace first."
}

New-Item -ItemType Directory -Force -Path $OutputRoot | Out-Null
$solutionRoot = Join-Path $OutputRoot "clean_root"
New-Item -ItemType Directory -Force -Path $solutionRoot | Out-Null

$regularExe = Join-Path $OutputRoot "demo_release_4k.exe"
$crinkledExe = Join-Path $OutputRoot "demo_crinkled_4k.exe"

function Invoke-BuildCli {
    param(
        [string[]]$CliArgs
    )

    & $cliPath @CliArgs
    if ($LASTEXITCODE -ne 0) {
        throw "Build CLI failed with exit code $LASTEXITCODE"
    }
}

function Assert-FileExists {
    param([string]$Path)
    if (-not (Test-Path $Path)) {
        throw "Expected file not found: $Path"
    }
}

function Assert-MaxSize {
    param(
        [string]$Path,
        [int64]$MaxBytes,
        [string]$Label
    )
    $size = (Get-Item $Path).Length
    if ($size -gt $MaxBytes) {
        throw "$Label size check failed. Size=$size bytes, Max=$MaxBytes bytes"
    }
    Write-Host "$Label size: $size bytes (max $MaxBytes)" -ForegroundColor Green
}

function Assert-NotContainsAscii {
    param(
        [byte[]]$Bytes,
        [string]$Needle,
        [string]$Label
    )
    $text = [System.Text.Encoding]::ASCII.GetString($Bytes)
    if ($text.Contains($Needle)) {
        throw "$Label contains unexpected asset marker: $Needle"
    }
}

Write-Host "Running regular tiny Release build (no Crinkler)..." -ForegroundColor Cyan
Invoke-BuildCli -CliArgs @(
    "--app-root", $repoRoot,
    "--project", $ProjectPath,
    "--output", $regularExe,
    "--solution-root", $solutionRoot,
    "--mode", "release",
    "--size", "4k",
    "--restricted-compact-track"
)

Write-Host "Running tiny Release Crinkled build..." -ForegroundColor Cyan
Invoke-BuildCli -CliArgs @(
    "--app-root", $repoRoot,
    "--project", $ProjectPath,
    "--output", $crinkledExe,
    "--solution-root", $solutionRoot,
    "--mode", "crinkled",
    "--size", "4k",
    "--restricted-compact-track"
)

Assert-FileExists -Path $regularExe
Assert-FileExists -Path $crinkledExe

Assert-MaxSize -Path $crinkledExe -MaxBytes 4096 -Label "Crinkled tiny exe"
Assert-MaxSize -Path $regularExe -MaxBytes $RegularMaxBytes -Label "Regular tiny exe"

$regularBytes = [System.IO.File]::ReadAllBytes($regularExe)
$crinkledBytes = [System.IO.File]::ReadAllBytes($crinkledExe)

$unusedTransitions = @(
    "assets/shaders/transition_crossfade.cso",
    "assets/shaders/transition_dip_to_black.cso",
    "assets/shaders/transition_glitch.cso",
    "assets/shaders/transition_pixelate.cso"
)

foreach ($needle in $unusedTransitions) {
    Assert-NotContainsAscii -Bytes $regularBytes -Needle $needle -Label "Regular tiny exe"
    Assert-NotContainsAscii -Bytes $crinkledBytes -Needle $needle -Label "Crinkled tiny exe"
}

Write-Host "Integration test passed." -ForegroundColor Green
