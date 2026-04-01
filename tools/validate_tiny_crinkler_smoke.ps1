param(
    [string]$BuildDir = "build_tiny_crinkler_smoke",
    [switch]$NoClean
)

$ErrorActionPreference = "Stop"

function Get-Vcvars32Path {
    $vsWhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path $vsWhere) {
        $vsPath = & $vsWhere -latest -property installationPath
        if ($vsPath) {
            $candidate = Join-Path $vsPath "VC\Auxiliary\Build\vcvars32.bat"
            if (Test-Path $candidate) {
                return $candidate
            }
        }
    }

    $fallbacks = @(
        "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars32.bat",
        "C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars32.bat",
        "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvars32.bat",
        "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars32.bat"
    )

    foreach ($candidate in $fallbacks) {
        if (Test-Path $candidate) {
            return $candidate
        }
    }

    return $null
}

function Import-VcvarsEnv {
    param([string]$VcvarsPath)

    if (-not $VcvarsPath) {
        throw "Unable to locate vcvars32.bat"
    }

    $lines = cmd /c "call `"$VcvarsPath`" >nul && set"
    foreach ($line in $lines) {
        if ($line -match "^(.*?)=(.*)$") {
            Set-Item -Path ("Env:" + $matches[1]) -Value $matches[2]
        }
    }
}

function New-SmokeBatchScript {
    param(
        [string]$ScriptPath,
        [string]$VcvarsPath,
        [string]$RepoRoot,
        [string]$BuildPath,
        [string]$CrinklerPath
    )

    $repoRootCmd = $RepoRoot -replace '/', '\\'
    $buildDirCmd = Split-Path -Leaf $BuildPath
    $crinklerPathCmd = "third_party\\Crinkler.exe"

    $lines = @(
        "@echo off",
        "setlocal",
        ('pushd "' + $repoRootCmd + '"'),
        ('call "' + $VcvarsPath + '"'),
        "if errorlevel 1 exit /b 1",
        "echo [tiny-crinkler-smoke] VSCMD_ARG_TGT_ARCH=%VSCMD_ARG_TGT_ARCH%",
        ('cmake -S . -B "' + $buildDirCmd + '" -G Ninja -DSHADERLAB_BUILD_EDITOR=OFF -DSHADERLAB_BUILD_RUNTIME=OFF -DSHADERLAB_BUILD_MICRO_PLAYER=ON -DSHADERLAB_USE_CRINKLER=ON -DSHADERLAB_CRINKLER_TINYIMPORT=OFF -DCRINKLER_PATH=' + $crinklerPathCmd + ' -DSHADERLAB_TINY_RUNTIME_COMPILE=OFF -DSHADERLAB_ENABLE_DXC=OFF -DCMAKE_BUILD_TYPE=Release'),
        "if errorlevel 1 exit /b 1",
        ('cmake --build "' + $buildDirCmd + '" --target ShaderLabMicroPlayer --verbose'),
        "set _exitcode=%errorlevel%",
        "popd",
        "exit /b %_exitcode%",
        ""
    )

    Set-Content -Path $ScriptPath -Value $lines -Encoding Ascii
}

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$crinklerPath = Join-Path $repoRoot "third_party\Crinkler.exe"

if (-not (Test-Path $crinklerPath)) {
    throw "Crinkler not found: $crinklerPath"
}

$vcvars32 = Get-Vcvars32Path
if (-not $vcvars32) {
    throw "Unable to locate vcvars32.bat"
}

$buildPath = Join-Path $repoRoot $BuildDir
if ((-not $NoClean) -and (Test-Path $buildPath)) {
    Remove-Item $buildPath -Recurse -Force -ErrorAction SilentlyContinue
}

New-Item -ItemType Directory -Path $buildPath -Force | Out-Null

$batchScriptPath = Join-Path $buildPath "tiny_crinkler_smoke_build.cmd"
New-SmokeBatchScript -ScriptPath $batchScriptPath -VcvarsPath $vcvars32 -RepoRoot $repoRoot -BuildPath $buildPath -CrinklerPath $crinklerPath

$machinePath = [Environment]::GetEnvironmentVariable("Path", "Machine")
$userPath = [Environment]::GetEnvironmentVariable("Path", "User")
if (-not [string]::IsNullOrWhiteSpace($machinePath)) {
    if (-not [string]::IsNullOrWhiteSpace($userPath)) {
        $env:PATH = "$machinePath;$userPath"
    } else {
        $env:PATH = $machinePath
    }
}

Write-Host "[tiny-crinkler-smoke] Running batch driver: $batchScriptPath" -ForegroundColor Cyan
$previousErrorAction = $ErrorActionPreference
$ErrorActionPreference = "Continue"
$buildOutput = & cmd /c $batchScriptPath 2>&1
$nativeExit = $LASTEXITCODE
$ErrorActionPreference = $previousErrorAction
$buildOutput | ForEach-Object { Write-Host $_ }

if ($nativeExit -ne 0) {
    if (($buildOutput -join "`n") -match "Unsupported file type") {
        throw "CMake build failed and Crinkler reported unsupported object type. Verify the smoke ran in x86 mode and that generated objects are x86."
    }
    if (($buildOutput -join "`n") -match "__filter_x86_sse2_floating_point_exception") {
        throw "CMake build failed in Crinkler link due CRT symbol mismatch (__filter_x86_sse2_floating_point_exception). Tiny+Crinkler must use compatible legacy x86 CRT import set."
    }
    throw "CMake build failed with exit code $nativeExit"
}

if (($buildOutput -join "`n") -notmatch "VSCMD_ARG_TGT_ARCH=x86") {
    throw "Smoke test failed: build did not run under x86 toolchain environment."
}

if (($buildOutput -join "`n") -notmatch "Crinkler\.exe") {
    throw "Smoke test failed: Crinkler linker invocation not detected in build output."
}

$exePath = Join-Path $buildPath "bin\ShaderLabMicroPlayer.exe"
if (-not (Test-Path $exePath)) {
    throw "Smoke test failed: expected output missing: $exePath"
}

$exe = Get-Item $exePath
Write-Host "[tiny-crinkler-smoke] OK: $($exe.FullName) ($($exe.Length) bytes)" -ForegroundColor Green
exit 0
