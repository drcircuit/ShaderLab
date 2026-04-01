param(
    [string]$ProjectPath = "",
    [string]$OutputRoot = ""
)

$ErrorActionPreference = "Stop"

$repo = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$cliCandidates = @(
    (Join-Path $repo "build_debug\bin\ShaderLabBuildCli.exe"),
    (Join-Path $repo "build\bin\ShaderLabBuildCli.exe")
)
$cli = $cliCandidates | Where-Object { Test-Path $_ } | Select-Object -First 1

function Assert-ExcludedProjectPath {
    param([string]$Path)

    if ([string]::IsNullOrWhiteSpace($Path)) {
        return $true
    }

    $normalized = $Path.Replace('/', '\').ToLowerInvariant()
    $excludedMarkers = @(
        "\build\\",
        "\build_debug\\",
        "\build_release\\",
        "\build_smoke_",
        "\build_m6_",
        "\build_tiny_",
        "\build_micro_",
        "\artifacts\\",
        "\.git\\",
        "\third_party\\"
    )

    foreach ($marker in $excludedMarkers) {
        if ($normalized.Contains($marker)) {
            return $true
        }
    }

    return $false
}

if ([string]::IsNullOrWhiteSpace($ProjectPath)) {
    $searchRoots = @(
        (Join-Path $repo "creative"),
        $repo
    )

    foreach ($root in $searchRoots) {
        if (-not (Test-Path $root)) {
            continue
        }

        $candidate = Get-ChildItem -Path $root -Recurse -File -Filter "project.json" -ErrorAction SilentlyContinue |
            Where-Object { -not (Assert-ExcludedProjectPath $_.FullName) } |
            Sort-Object FullName |
            Select-Object -First 1

        if ($candidate) {
            $ProjectPath = $candidate.FullName
            break
        }
    }
}

if ([string]::IsNullOrWhiteSpace($cli)) {
    throw "ShaderLabBuildCli missing. Expected one of: $($cliCandidates -join ', ')"
}
if (-not (Test-Path $ProjectPath)) {
    throw "Project JSON missing: $ProjectPath. Pass -ProjectPath or provide a source-controlled project.json."
}

if ([string]::IsNullOrWhiteSpace($OutputRoot)) {
    $OutputRoot = Join-Path $repo "build_smoke_crinkled"
}
New-Item -ItemType Directory -Force -Path $OutputRoot | Out-Null

$runStamp = Get-Date -Format "yyyyMMdd_HHmmss"
$runId = "${runStamp}_$PID"

function Invoke-Crinkled {
    param(
        [string]$Name,
        [string]$Target,
        [string]$Ext,
        [string]$Size = "none"
    )

    $root = Join-Path $OutputRoot ("root_" + $Name + "_" + $runId)
    $out = Join-Path $OutputRoot ("artifact_" + $Name + $Ext)

    Write-Host "--- $Name (crinkled) ---" -ForegroundColor Cyan
    & $cli --app-root $repo --project $ProjectPath --output $out --solution-root $root --target $Target --mode crinkled --size $Size | Out-Host

    $code = $LASTEXITCODE
    $exists = Test-Path $out
    $sizeBytes = if ($exists) { (Get-Item $out).Length } else { -1 }
    $sizeText = if ($exists) { "{0:N0}" -f $sizeBytes } else { "n/a" }

    [PSCustomObject]@{
        Name = $Name
        Target = $Target
        ExitCode = $code
        Exists = $exists
        SizeBytes = $sizeBytes
        Size = $sizeText
        Output = $out
    }
}

$results = @()
$results += Invoke-Crinkled -Name "packaged" -Target "packaged" -Ext ".zip"
$results += Invoke-Crinkled -Name "selfcontained" -Target "selfcontained" -Ext ".exe"
$results += Invoke-Crinkled -Name "screensaver" -Target "screensaver" -Ext ".scr"
$results += Invoke-Crinkled -Name "micro" -Target "micro" -Ext ".exe" -Size "64k"

Write-Host "--- SUMMARY ---" -ForegroundColor Yellow
$results | Format-Table -AutoSize

$failures = $results | Where-Object { -not ($_.ExitCode -eq 0 -and $_.Exists) }
if ($failures.Count -eq 0) {
    Write-Host "ALL_CRINKLED_OK" -ForegroundColor Green
    exit 0
}

Write-Host "CRINKLED_MATRIX_HAS_FAILURES" -ForegroundColor Red
exit 1
