param(
    [string]$BuildBin = ""
)

$ErrorActionPreference = "Stop"

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
if ([string]::IsNullOrWhiteSpace($BuildBin)) {
    $BuildBin = Join-Path $repoRoot "build\bin"
} elseif (-not [System.IO.Path]::IsPathRooted($BuildBin)) {
    $BuildBin = Join-Path $repoRoot $BuildBin
}

$source = Join-Path $repoRoot "docs\enduser"
if (-not (Test-Path $source)) {
    Write-Host "No end-user docs found at $source, skipping." -ForegroundColor Yellow
    exit 0
}

if (-not (Test-Path $BuildBin)) {
    throw "Build output folder not found: $BuildBin"
}

$destRoot = Join-Path $BuildBin "docs"
$dest = Join-Path $destRoot "enduser"

New-Item -ItemType Directory -Force -Path $destRoot | Out-Null
if (Test-Path $dest) {
    Remove-Item -Path $dest -Recurse -Force
}
Copy-Item -Path $source -Destination $destRoot -Recurse -Force

Write-Host "Staged end-user docs to: $dest" -ForegroundColor Green
