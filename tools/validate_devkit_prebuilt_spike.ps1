param(
    [string]$ProjectPath = "",
    [string]$OutputRoot = "",
    [switch]$SkipLaunchMatrix
)

$ErrorActionPreference = "Stop"

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path

function Test-ExcludedProjectPath {
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

function Resolve-DefaultProjectPath {
    param([string]$RepoRoot)

    $directCandidates = @(
        (Join-Path $RepoRoot "experiments\cloud_tunnel\project.json")
    )

    foreach ($candidate in $directCandidates) {
        if (Test-Path $candidate) {
            return (Resolve-Path $candidate).Path
        }
    }

    $searchRoots = @(
        (Join-Path $RepoRoot "creative"),
        $RepoRoot
    )

    foreach ($root in $searchRoots) {
        if (-not (Test-Path $root)) {
            continue
        }

        $projectCandidates = Get-ChildItem -Path $root -Recurse -File -Filter "project.json" -ErrorAction SilentlyContinue |
            Where-Object { -not (Test-ExcludedProjectPath $_.FullName) } |
            Sort-Object FullName

        $first = $projectCandidates | Select-Object -First 1
        if ($first) {
            return $first.FullName
        }
    }

    return ""
}

function Assert-True {
    param(
        [bool]$Condition,
        [string]$Message
    )

    if (-not $Condition) {
        throw $Message
    }
}

function Assert-RelativeAssetPath {
    param(
        [string]$PathValue,
        [string]$FieldName
    )

    if ([string]::IsNullOrWhiteSpace($PathValue)) {
        return
    }

    Assert-True -Condition (-not [System.IO.Path]::IsPathRooted($PathValue)) -Message "$FieldName must be relative: $PathValue"
    Assert-True -Condition (-not $PathValue.Contains("..")) -Message "$FieldName must not contain '..': $PathValue"
}

Write-Host "M6 Dev Kit Prebuilt Packaging Spike" -ForegroundColor Cyan
Write-Host "===================================" -ForegroundColor Cyan

$cliCandidates = @(
    (Join-Path $repoRoot "build_debug\bin\ShaderLabBuildCli.exe"),
    (Join-Path $repoRoot "build\bin\ShaderLabBuildCli.exe")
)
$buildCliPath = $cliCandidates | Where-Object { Test-Path $_ } | Select-Object -First 1
Assert-True -Condition (-not [string]::IsNullOrWhiteSpace($buildCliPath)) -Message "ShaderLabBuildCli missing. Expected one of: $($cliCandidates -join ', ')"

if ([string]::IsNullOrWhiteSpace($ProjectPath)) {
    $ProjectPath = Resolve-DefaultProjectPath -RepoRoot $repoRoot
}
Assert-True -Condition (-not [string]::IsNullOrWhiteSpace($ProjectPath) -and (Test-Path $ProjectPath)) -Message "Project JSON missing. Pass -ProjectPath or add a project candidate."

if ([string]::IsNullOrWhiteSpace($OutputRoot)) {
    $OutputRoot = Join-Path $repoRoot "build_m6_prebuilt_spike"
}

if (Test-Path $OutputRoot) {
    Remove-Item -Path $OutputRoot -Recurse -Force -ErrorAction SilentlyContinue
}
New-Item -ItemType Directory -Path $OutputRoot -Force | Out-Null

$artifactPath = Join-Path $OutputRoot "artifact_m6_prebuilt.zip"
$solutionRoot = Join-Path $OutputRoot "root_m6_prebuilt"
$buildLogPath = Join-Path $OutputRoot "log_m6_prebuilt_build.txt"
$extractRoot = Join-Path $OutputRoot "artifact_m6_prebuilt"

Write-Host "[1/4] Build reference prebuilt package" -ForegroundColor Cyan
& $buildCliPath --app-root $repoRoot --project $ProjectPath --output $artifactPath --solution-root $solutionRoot --target packaged --mode release --size none --restricted-compact-track *> $buildLogPath
if ($LASTEXITCODE -ne 0) {
    throw "BuildCli returned exit code $LASTEXITCODE. See: $buildLogPath"
}
Assert-True -Condition (Test-Path $artifactPath) -Message "Expected artifact missing: $artifactPath"

Write-Host "[2/4] Validate packaged layout" -ForegroundColor Cyan
if (Test-Path $extractRoot) {
    Remove-Item -Path $extractRoot -Recurse -Force -ErrorAction SilentlyContinue
}
Expand-Archive -Path $artifactPath -DestinationPath $extractRoot -Force

$runtimeExe = Get-ChildItem -Path $extractRoot -File -Filter "*.exe" -ErrorAction SilentlyContinue | Select-Object -First 1
Assert-True -Condition ($null -ne $runtimeExe) -Message "Packaged zip does not contain a runtime executable at root."

$manifestPath = Join-Path $extractRoot "project.json"
$assetsRoot = Join-Path $extractRoot "assets"
$trackBinPath = Join-Path $assetsRoot "track.bin"
$shadersRoot = Join-Path $assetsRoot "shaders"
$vertexCsoPath = Join-Path $shadersRoot "vertex.cso"

Assert-True -Condition (Test-Path $manifestPath) -Message "project.json missing in packaged zip."
Assert-True -Condition (Test-Path $assetsRoot) -Message "assets directory missing in packaged zip."
Assert-True -Condition (Test-Path $trackBinPath) -Message "assets/track.bin missing (compact track payload contract)."
Assert-True -Condition (Test-Path $vertexCsoPath) -Message "assets/shaders/vertex.cso missing (precompiled shader contract)."

$csoFiles = Get-ChildItem -Path $shadersRoot -Filter "*.cso" -File -ErrorAction SilentlyContinue
Assert-True -Condition ($csoFiles.Count -gt 0) -Message "No precompiled .cso shaders found under assets/shaders/."

Write-Host "[3/4] Validate runtime loader contract from manifest" -ForegroundColor Cyan
$manifest = Get-Content -Path $manifestPath -Raw | ConvertFrom-Json

$rows = @()
if ($null -ne $manifest.track -and $null -ne $manifest.track.rows) {
    $rows = @($manifest.track.rows)
}
Assert-True -Condition ($rows.Count -eq 0) -Message "Expected compact-track manifest to have zero tracker rows when track.bin is present."

$scenes = @()
if ($null -ne $manifest.scenes) {
    $scenes = @($manifest.scenes)
}
Assert-True -Condition ($scenes.Count -gt 0) -Message "Manifest contains no scenes; expected at least one scene."

foreach ($scene in $scenes) {
    $scenePrecompiled = [string]$scene.precompiled
    Assert-True -Condition (-not [string]::IsNullOrWhiteSpace($scenePrecompiled)) -Message "Scene missing precompiled path in manifest."
    Assert-RelativeAssetPath -PathValue $scenePrecompiled -FieldName "scene.precompiled"

    $scenePrecompiledPath = Join-Path $extractRoot $scenePrecompiled
    Assert-True -Condition (Test-Path $scenePrecompiledPath) -Message "Scene precompiled shader missing: $scenePrecompiled"

    $sceneCode = [string]$scene.code
    Assert-True -Condition ([string]::IsNullOrWhiteSpace($sceneCode)) -Message "Scene shader source should be stripped for prebuilt compact package."

    $bindings = @()
    if ($null -ne $scene.bindings) {
        $bindings = @($scene.bindings)
    }

    foreach ($bind in $bindings) {
        $bindType = [string]$bind.bindType
        $bindPath = [string]$bind.path
        if ($bindType -eq "File" -and -not [string]::IsNullOrWhiteSpace($bindPath)) {
            Assert-RelativeAssetPath -PathValue $bindPath -FieldName "binding.path"
            $resolvedBindPath = Join-Path $extractRoot $bindPath
            Assert-True -Condition (Test-Path $resolvedBindPath) -Message "Texture/audio binding file missing: $bindPath"
        }
    }

    $postFx = @()
    if ($null -ne $scene.postfx) {
        $postFx = @($scene.postfx)
    }

    foreach ($fx in $postFx) {
        $enabled = $true
        if ($null -ne $fx.enabled) {
            $enabled = [bool]$fx.enabled
        }
        if (-not $enabled) {
            continue
        }

        $fxPrecompiled = [string]$fx.precompiled
        Assert-True -Condition (-not [string]::IsNullOrWhiteSpace($fxPrecompiled)) -Message "Enabled post-FX missing precompiled path in manifest."
        Assert-RelativeAssetPath -PathValue $fxPrecompiled -FieldName "postfx.precompiled"

        $fxPrecompiledPath = Join-Path $extractRoot $fxPrecompiled
        Assert-True -Condition (Test-Path $fxPrecompiledPath) -Message "Enabled post-FX precompiled shader missing: $fxPrecompiled"

        $fxCode = [string]$fx.code
        Assert-True -Condition ([string]::IsNullOrWhiteSpace($fxCode)) -Message "Enabled post-FX shader source should be stripped for prebuilt compact package."
    }
}

$audioClips = @()
if ($null -ne $manifest.audio) {
    $audioClips = @($manifest.audio)
}
foreach ($clip in $audioClips) {
    $clipPath = [string]$clip.path
    if ([string]::IsNullOrWhiteSpace($clipPath)) {
        continue
    }
    Assert-RelativeAssetPath -PathValue $clipPath -FieldName "audio.path"
    $resolvedClipPath = Join-Path $extractRoot $clipPath
    Assert-True -Condition (Test-Path $resolvedClipPath) -Message "Audio clip file missing: $clipPath"
}

if (-not $SkipLaunchMatrix) {
    Write-Host "[4/4] Run startup/relaunch matrix" -ForegroundColor Cyan
    $matrixLogPath = Join-Path $OutputRoot "log_m6_prebuilt_startup_matrix.txt"
    & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot "validate_startup_relaunch_matrix.ps1") -ProjectPath $ProjectPath -OutputRoot (Join-Path $OutputRoot "startup_relaunch") *> $matrixLogPath
    if ($LASTEXITCODE -ne 0) {
        throw "Startup/relaunch matrix failed. See: $matrixLogPath"
    }
} else {
    Write-Host "[4/4] Startup/relaunch matrix skipped by flag" -ForegroundColor DarkYellow
}

Write-Host "M6_PREBUILT_SPIKE_OK" -ForegroundColor Green
