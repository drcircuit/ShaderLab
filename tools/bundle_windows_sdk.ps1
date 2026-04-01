param(
    [string]$OutputRoot = "",
    [string]$WindowsKitsRoot = "",
    [string]$Version = ""
)

$ErrorActionPreference = "Stop"

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
if ([string]::IsNullOrWhiteSpace($OutputRoot)) {
    $OutputRoot = Join-Path $repoRoot "third_party\windows_sdk_bundle"
}
if (-not [System.IO.Path]::IsPathRooted($OutputRoot)) {
    $OutputRoot = Join-Path $repoRoot $OutputRoot
}

if ([string]::IsNullOrWhiteSpace($WindowsKitsRoot)) {
    $WindowsKitsRoot = Join-Path ${env:ProgramFiles(x86)} "Windows Kits\10"
}

$includeRoot = Join-Path $WindowsKitsRoot "Include"
$libRoot = Join-Path $WindowsKitsRoot "Lib"
$binRoot = Join-Path $WindowsKitsRoot "bin"

if (-not (Test-Path $includeRoot)) { throw "Windows SDK Include root not found: $includeRoot" }
if (-not (Test-Path $libRoot)) { throw "Windows SDK Lib root not found: $libRoot" }
if (-not (Test-Path $binRoot)) { throw "Windows SDK bin root not found: $binRoot" }

function Resolve-Version {
    param([string]$RequestedVersion)

    if (-not [string]::IsNullOrWhiteSpace($RequestedVersion)) {
        return $RequestedVersion
    }

    $candidates = Get-ChildItem -Path $includeRoot -Directory | Sort-Object Name -Descending
    foreach ($dir in $candidates) {
        $name = $dir.Name
        $hasHeaders = Test-Path (Join-Path $includeRoot "$name\um\d3d12.h")
        $hasLibs = (Test-Path (Join-Path $libRoot "$name\um\x86\kernel32.lib")) -and
                   (Test-Path (Join-Path $libRoot "$name\um\x64\kernel32.lib")) -and
                   (Test-Path (Join-Path $libRoot "$name\ucrt\x86\ucrt.lib")) -and
                   (Test-Path (Join-Path $libRoot "$name\ucrt\x64\ucrt.lib"))
        $hasTools = (Test-Path (Join-Path $binRoot "$name\x86\rc.exe")) -and
                    (Test-Path (Join-Path $binRoot "$name\x64\rc.exe"))
        if ($hasHeaders -and $hasLibs -and $hasTools) {
            return $name
        }
    }

    throw "No usable Windows SDK version found under $includeRoot"
}

$resolvedVersion = Resolve-Version -RequestedVersion $Version
Write-Host "Using Windows SDK version: $resolvedVersion" -ForegroundColor Cyan

$srcIncludeVersion = Join-Path $includeRoot $resolvedVersion
$srcLibVersion = Join-Path $libRoot $resolvedVersion
$srcBinVersion = Join-Path $binRoot $resolvedVersion

$dstIncludeVersion = Join-Path $OutputRoot "Include\$resolvedVersion"
$dstLibVersion = Join-Path $OutputRoot "Lib\$resolvedVersion"
$dstBinVersion = Join-Path $OutputRoot "bin\$resolvedVersion"

New-Item -ItemType Directory -Force -Path $dstIncludeVersion | Out-Null
New-Item -ItemType Directory -Force -Path $dstLibVersion | Out-Null
New-Item -ItemType Directory -Force -Path $dstBinVersion | Out-Null

$includeSubdirs = @("ucrt", "shared", "um", "winrt", "cppwinrt")
foreach ($subdir in $includeSubdirs) {
    $src = Join-Path $srcIncludeVersion $subdir
    if (Test-Path $src) {
        Copy-Item -Path $src -Destination $dstIncludeVersion -Recurse -Force
    }
}

$libSubdirs = @("ucrt", "um")
foreach ($subdir in $libSubdirs) {
    $src = Join-Path $srcLibVersion $subdir
    if (Test-Path $src) {
        Copy-Item -Path $src -Destination $dstLibVersion -Recurse -Force
    }
}

$archs = @("x86", "x64")
$toolFiles = @("rc.exe", "rcdll.dll", "mt.exe")
foreach ($arch in $archs) {
    $srcArchDir = Join-Path $srcBinVersion $arch
    if (-not (Test-Path $srcArchDir)) { continue }

    $dstArchDir = Join-Path $dstBinVersion $arch
    New-Item -ItemType Directory -Force -Path $dstArchDir | Out-Null

    foreach ($tool in $toolFiles) {
        $srcTool = Join-Path $srcArchDir $tool
        if (Test-Path $srcTool) {
            Copy-Item -Path $srcTool -Destination $dstArchDir -Force
        }
    }
}

$manifest = [ordered]@{
    schema = 1
    sourceWindowsKitsRoot = $WindowsKitsRoot
    sdkVersion = $resolvedVersion
    generatedAtUtc = (Get-Date).ToUniversalTime().ToString("o")
}
$manifestPath = Join-Path $OutputRoot "bundle_manifest.json"
$manifest | ConvertTo-Json -Depth 5 | Set-Content -Path $manifestPath -Encoding UTF8

Write-Host "Bundled SDK written to: $OutputRoot" -ForegroundColor Green
Write-Host "Manifest: $manifestPath" -ForegroundColor Green
