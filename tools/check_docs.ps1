param(
    [switch]$Strict
)

$ErrorActionPreference = "Stop"

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$docsRoot = Join-Path $repoRoot "docs"

Write-Host "ShaderLab Documentation Check" -ForegroundColor Cyan
Write-Host "=============================" -ForegroundColor Cyan
Write-Host ""

$ok = $true

function Test-MarkdownLinks {
    param(
        [string]$FilePath,
        [string]$Root
    )

    $content = Get-Content -Raw -Path $FilePath
    $matches = [regex]::Matches($content, "\[[^\]]+\]\(([^\)]+)\)")
    $localLinks = @()

    foreach ($match in $matches) {
        $target = $match.Groups[1].Value.Trim()
        if ($target -match "^(https?://|mailto:|#)") {
            continue
        }

        $pathOnly = $target.Split('#')[0]
        if ([string]::IsNullOrWhiteSpace($pathOnly)) {
            continue
        }

        $resolved = Join-Path (Split-Path -Parent $FilePath) $pathOnly
        $resolved = [System.IO.Path]::GetFullPath($resolved)
        $localLinks += $resolved
    }

    foreach ($linkPath in $localLinks | Sort-Object -Unique) {
        if (-not (Test-Path $linkPath)) {
            Write-Host "Broken link in $FilePath -> $linkPath" -ForegroundColor Red
            $script:ok = $false
        }
    }
}

$requiredDocs = @(
    "README.md",
    "docs\README.md",
    "docs\QUICKSTART.md",
    "docs\BUILD.md",
    "docs\ARCHITECTURE.md",
    "docs\STRUCTURE.md",
    "docs\CONTRIBUTING.md"
)

Write-Host "Checking required docs..." -ForegroundColor Yellow
foreach ($relativePath in $requiredDocs) {
    $absolutePath = Join-Path $repoRoot $relativePath
    if (Test-Path $absolutePath) {
        Write-Host "  OK: $relativePath" -ForegroundColor Green
    }
    else {
        Write-Host "  Missing: $relativePath" -ForegroundColor Red
        $ok = $false
    }
}

Write-Host ""
Write-Host "Checking archived status docs..." -ForegroundColor Yellow
$archivedPointers = @(
    "BOOTSTRAP_COMPLETE.md",
    "SETUP_STATUS.md",
    "IMPLEMENTATION_SUMMARY.md"
)

foreach ($relativePath in $archivedPointers) {
    $absolutePath = Join-Path $repoRoot $relativePath
    if (-not (Test-Path $absolutePath)) {
        Write-Host "  Missing: $relativePath" -ForegroundColor Red
        $ok = $false
        continue
    }

    $content = Get-Content -Raw -Path $absolutePath
    if ($content -match "# Archived Document") {
        Write-Host "  OK: $relativePath" -ForegroundColor Green
    }
    else {
        Write-Host "  Not archived format: $relativePath" -ForegroundColor Red
        $ok = $false
    }
}

Write-Host ""
Write-Host "Checking markdown links..." -ForegroundColor Yellow
$markdownFiles = Get-ChildItem -Path $repoRoot -Recurse -File -Filter "*.md" |
    Where-Object { $_.FullName -notmatch "\\build(\\|$)" }

foreach ($mdFile in $markdownFiles) {
    Test-MarkdownLinks -FilePath $mdFile.FullName -Root $repoRoot
}

Write-Host ""
if ($ok) {
    Write-Host "Documentation check passed." -ForegroundColor Green
    exit 0
}

Write-Host "Documentation check failed." -ForegroundColor Red
if ($Strict) {
    exit 1
}

exit 1
