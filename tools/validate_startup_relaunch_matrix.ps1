param(
    [string]$ProjectPath = "",
    [string]$OutputRoot = "",
    [int]$SmokeSeconds = 3
)

$ErrorActionPreference = "Stop"

$repo = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path

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

function Resolve-DefaultProjectPath {
    param([string]$RepoRoot)

    $searchRoots = @(
        (Join-Path $RepoRoot "creative"),
        $RepoRoot
    )

    foreach ($root in $searchRoots) {
        if (-not (Test-Path $root)) {
            continue
        }

        $projectCandidates = Get-ChildItem -Path $root -Recurse -File -Filter "project.json" -ErrorAction SilentlyContinue |
            Where-Object { -not (Assert-ExcludedProjectPath $_.FullName) } |
            Sort-Object FullName

        $first = $projectCandidates | Select-Object -First 1
        if ($first) {
            return $first.FullName
        }
    }

    return ""
}

$cliCandidates = @(
    (Join-Path $repo "build_debug\bin\ShaderLabBuildCli.exe"),
    (Join-Path $repo "build\bin\ShaderLabBuildCli.exe")
)
$cli = $cliCandidates | Where-Object { Test-Path $_ } | Select-Object -First 1

if ([string]::IsNullOrWhiteSpace($ProjectPath)) {
    $ProjectPath = Resolve-DefaultProjectPath -RepoRoot $repo
}

if (-not $cli) {
    throw "ShaderLabBuildCli missing. Expected one of: $($cliCandidates -join ', ')"
}
if ([string]::IsNullOrWhiteSpace($ProjectPath) -or -not (Test-Path $ProjectPath)) {
    throw "Project JSON missing. Pass -ProjectPath or provide a source-controlled project.json (for example under experiments/ or creative/)."
}
if ($SmokeSeconds -lt 1) {
    $SmokeSeconds = 1
}

if ([string]::IsNullOrWhiteSpace($OutputRoot)) {
    $OutputRoot = Join-Path $repo "build_smoke_startup_relaunch"
}
New-Item -ItemType Directory -Force -Path $OutputRoot | Out-Null

function Invoke-BuildArtifact {
    param(
        [string]$Name,
        [string]$Target,
        [string]$Ext,
        [string]$Mode = "release",
        [string]$Size = "none"
    )

    $root = Join-Path $OutputRoot ("root_" + $Name)
    $out = Join-Path $OutputRoot ("artifact_" + $Name + $Ext)
    $log = Join-Path $OutputRoot ("log_build_" + $Name + ".txt")

    Write-Host "--- Build $Name ($Mode) ---" -ForegroundColor Cyan
    $args = @(
        "--app-root", $repo,
        "--project", $ProjectPath,
        "--output", $out,
        "--solution-root", $root,
        "--target", $Target,
        "--mode", $Mode,
        "--size", $Size
    )
    & $cli @args *> $log

    $exists = Test-Path $out
    $sizeBytes = if ($exists) { (Get-Item $out).Length } else { -1 }
    $sizeText = if ($exists) { "{0:N0}" -f $sizeBytes } else { "n/a" }

    [PSCustomObject]@{
        Name = $Name
        Target = $Target
        Mode = $Mode
        Output = $out
        BuildExitCode = $LASTEXITCODE
        BuildLog = $log
        Exists = $exists
        SizeBytes = $sizeBytes
        Size = $sizeText
        Notes = ""
    }
}

function Test-ProjectCompilationFailure {
    param([string]$LogPath)

    if (-not (Test-Path $LogPath)) {
        return $false
    }

    $content = Get-Content -Path $LogPath -Raw -ErrorAction SilentlyContinue
    if ([string]::IsNullOrWhiteSpace($content)) {
        return $false
    }

    return ($content -match "Scene shader precompile failed" -or
            $content -match "Failed to parse project" -or
            $content -match "Error:.*project")
}

function Invoke-RelaunchSmoke {
    param(
        [string]$Name,
        [string]$ExePath,
        [string]$Arguments = ""
    )

    if (-not (Test-Path $ExePath)) {
        return [PSCustomObject]@{
            Name = $Name
            Started1 = $false
            Started2 = $false
            Notes = "missing executable"
        }
    }

    $runtimeErrorLog = Join-Path (Split-Path $ExePath -Parent) "runtime_error.log"
    $shaderLabLog = Join-Path $env:TEMP "shaderlab_log.txt"
    $started1 = $false
    $started2 = $false
    $mutexConflictDetected = $false
    $runtimeFailureDetected = $false
    $shaderFailureDetected = $false

    for ($i = 1; $i -le 2; $i++) {
        $launchSucceeded = $false

        for ($attempt = 1; $attempt -le 2; $attempt++) {
            if (Test-Path $runtimeErrorLog) {
                Remove-Item $runtimeErrorLog -Force -ErrorAction SilentlyContinue
            }
            if (Test-Path $shaderLabLog) {
                Remove-Item $shaderLabLog -Force -ErrorAction SilentlyContinue
            }

            if ([string]::IsNullOrWhiteSpace($Arguments)) {
                $proc = Start-Process -FilePath $ExePath -PassThru -WindowStyle Minimized
            } else {
                $proc = Start-Process -FilePath $ExePath -ArgumentList $Arguments -PassThru -WindowStyle Minimized
            }
            Start-Sleep -Seconds $SmokeSeconds

            $runtimeErrors = @()
            if (Test-Path $runtimeErrorLog) {
                $runtimeErrors = Get-Content -Path $runtimeErrorLog -ErrorAction SilentlyContinue
            }
            $startupSkipped = ($runtimeErrors | Where-Object { $_ -match "single-instance mutex already exists" } | Select-Object -First 1)
            $otherRuntimeFailure = ($runtimeErrors | Where-Object {
                -not [string]::IsNullOrWhiteSpace($_) -and
                -not ($_ -match "single-instance mutex already exists")
            } | Select-Object -First 1)
            if ($startupSkipped) {
                $mutexConflictDetected = $true
            }
            if ($otherRuntimeFailure) {
                $runtimeFailureDetected = $true
            }

            $shaderLogLines = @()
            if (Test-Path $shaderLabLog) {
                $shaderLogLines = Get-Content -Path $shaderLabLog -ErrorAction SilentlyContinue
            }
            $shaderFailure = ($shaderLogLines | Where-Object {
                $_ -match "PreviewRenderer: Failed" -or
                $_ -match "Shader compilation failed" -or
                $_ -match "Failed to compile"
            } | Select-Object -First 1)
            if ($shaderFailure) {
                $shaderFailureDetected = $true
            }

            if (-not $proc.HasExited) {
                Stop-Process -Id $proc.Id -Force
                Wait-Process -Id $proc.Id -ErrorAction SilentlyContinue
                if (-not $startupSkipped -and -not $otherRuntimeFailure -and -not $shaderFailure) {
                    $launchSucceeded = $true
                    break
                }
            } else {
                if ($proc.ExitCode -eq 0 -and -not $startupSkipped -and -not $otherRuntimeFailure -and -not $shaderFailure) {
                    $launchSucceeded = $true
                    break
                }
            }

            if ($startupSkipped -and $attempt -lt 2) {
                Start-Sleep -Milliseconds 750
                continue
            }

            break
        }

        if ($launchSucceeded) {
            if ($i -eq 1) { $started1 = $true } else { $started2 = $true }
        }
    }

    $notes = @(
        $(if ($mutexConflictDetected) { "startup skipped (single-instance mutex)" }),
        $(if ($runtimeFailureDetected) { "runtime_error.log indicates startup/render failure" }),
        $(if ($shaderFailureDetected) { "shaderlab_log indicates shader compile/render failure" })
    ) | Where-Object { -not [string]::IsNullOrWhiteSpace($_) }

    if ($null -eq $notes) {
        $notes = @()
    } else {
        $notes = @($notes)
    }

    [PSCustomObject]@{
        Name = $Name
        Started1 = $started1
        Started2 = $started2
        Notes = [string]::Join("; ", $notes)
    }
}

$matrix = @(
    @{ Name = "selfcontained"; Target = "selfcontained"; Ext = ".exe"; Size = "none" },
    @{ Name = "packaged"; Target = "packaged"; Ext = ".zip"; Size = "none" },
    @{ Name = "screensaver"; Target = "screensaver"; Ext = ".scr"; Size = "none" },
    @{ Name = "micro"; Target = "micro"; Ext = ".exe"; Size = "64k" }
)

$builds = @()
$projectCompileFailureDetected = $false
$projectCompileFailureLog = ""

foreach ($entry in $matrix) {
    if ($projectCompileFailureDetected) {
        $builds += [PSCustomObject]@{
            Name = $entry.Name
            Target = $entry.Target
            Mode = "release"
            Output = (Join-Path $OutputRoot ("artifact_" + $entry.Name + $entry.Ext))
            BuildExitCode = -2
            BuildLog = $projectCompileFailureLog
            Exists = $false
            Notes = "skipped due project compile failure"
        }
        continue
    }

    $result = Invoke-BuildArtifact -Name $entry.Name -Target $entry.Target -Ext $entry.Ext -Size $entry.Size
    $builds += $result

    if ($result.BuildExitCode -ne 0 -and (Test-ProjectCompilationFailure -LogPath $result.BuildLog)) {
        $projectCompileFailureDetected = $true
        $projectCompileFailureLog = $result.BuildLog
        $result.Notes = "project compile failed"
    }
}

$launches = @()
if (-not $projectCompileFailureDetected) {
    $launches += Invoke-RelaunchSmoke -Name "selfcontained" -ExePath (($builds | Where-Object Name -eq "selfcontained").Output)
    $launches += Invoke-RelaunchSmoke -Name "screensaver" -ExePath (($builds | Where-Object Name -eq "screensaver").Output) -Arguments "/s"
    $launches += Invoke-RelaunchSmoke -Name "micro" -ExePath (($builds | Where-Object Name -eq "micro").Output)
}

Write-Host "--- BUILD SUMMARY ---" -ForegroundColor Yellow
$builds | Select-Object Name,Target,Mode,BuildExitCode,Exists,Size,Notes,Output | Format-Table -AutoSize

Write-Host "--- RELAUNCH SUMMARY ---" -ForegroundColor Yellow
if ($launches.Count -gt 0) {
    $launches | Select-Object Name,Started1,Started2,Notes | Format-Table -AutoSize
} else {
    Write-Host "Skipped (project compilation failed during build phase)." -ForegroundColor DarkYellow
}

if ($projectCompileFailureDetected) {
    Write-Host "Project compilation failure detected. Provide -ProjectPath to a known-good project for startup/relaunch validation." -ForegroundColor DarkYellow
}

$buildFailures = $builds | Where-Object { $_.BuildExitCode -ne -2 -and -not ($_.BuildExitCode -eq 0 -and $_.Exists) }
$buildSkipped = $builds | Where-Object { $_.BuildExitCode -eq -2 }
$launchFailures = $launches | Where-Object { -not ($_.Started1 -and $_.Started2) }

if ($buildFailures.Count -eq 0 -and $launchFailures.Count -eq 0) {
    Write-Host "STARTUP_RELAUNCH_MATRIX_OK" -ForegroundColor Green
    exit 0
}

if ($buildFailures.Count -gt 0) {
    Write-Host "Build failures:" -ForegroundColor Red
    foreach ($f in $buildFailures) {
        Write-Host " - $($f.Name) (log: $($f.BuildLog))" -ForegroundColor Yellow
    }
}

if ($buildSkipped.Count -gt 0) {
    Write-Host "Skipped targets:" -ForegroundColor DarkYellow
    foreach ($s in $buildSkipped) {
        Write-Host " - $($s.Name) ($($s.Notes))" -ForegroundColor DarkYellow
    }
}

if ($launchFailures.Count -gt 0) {
    Write-Host "Relaunch failures:" -ForegroundColor Red
    foreach ($f in $launchFailures) {
        Write-Host " - $($f.Name)" -ForegroundColor Yellow
    }
}

Write-Host "STARTUP_RELAUNCH_MATRIX_HAS_FAILURES" -ForegroundColor Red
exit 1
