# ShaderLab developer environment loader
param(
    [string]$ConfigPath = (Join-Path $PSScriptRoot "dev_env.cfg")
)

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

function Import-VcvarsEnv {
    param([string]$VcvarsPath)

    if (-not $VcvarsPath) {
        Write-Host "vcvars64.bat not found. Install Visual Studio Build Tools or update the path." -ForegroundColor Red
        return $false
    }

    $cmd = "call `"$VcvarsPath`" >nul && set"
    $lines = cmd /c $cmd
    foreach ($line in $lines) {
        if ($line -match "^(.*?)=(.*)$") {
            $name = $matches[1]
            $value = $matches[2]
            Set-Item -Path ("Env:" + $name) -Value $value
        }
    }

    return $true
}

function Add-PathEntriesFromCfg {
    param([string]$PathFile)

    if (-not (Test-Path $PathFile)) {
        Write-Host "Config not found: $PathFile" -ForegroundColor Yellow
        return
    }

    $existing = @{}
    foreach ($entry in ($env:PATH -split ";")) {
        if (-not [string]::IsNullOrWhiteSpace($entry)) {
            $existing[$entry] = $true
        }
    }

    $added = 0
    foreach ($raw in (Get-Content $PathFile)) {
        $line = $raw.Trim()
        if ([string]::IsNullOrWhiteSpace($line) -or $line.StartsWith("#")) {
            continue
        }

        $expanded = [Environment]::ExpandEnvironmentVariables($line)
        if (-not (Test-Path $expanded)) {
            Write-Host "Skipping missing path: $expanded" -ForegroundColor Yellow
            continue
        }

        if (-not $existing.ContainsKey($expanded)) {
            $env:PATH = "$expanded;$env:PATH"
            $existing[$expanded] = $true
            $added++
        }
    }

    if ($added -gt 0) {
        Write-Host "Added $added PATH entr$(if ($added -eq 1) { "y" } else { "ies" })." -ForegroundColor Green
    }
}

function Resolve-CrinklerPath {
    if ($env:SHADERLAB_CRINKLER -and (Test-Path $env:SHADERLAB_CRINKLER)) {
        return $env:SHADERLAB_CRINKLER
    }

    $cmd = Get-Command crinkler.exe -ErrorAction SilentlyContinue
    if ($cmd -and $cmd.Source) {
        return (Split-Path $cmd.Source -Parent)
    }

    $repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
    $repoCrinklerExe = Join-Path $repoRoot "third_party\Crinkler.exe"
    if (Test-Path $repoCrinklerExe) {
        return (Split-Path $repoCrinklerExe -Parent)
    }

    return $null
}

Write-Host "Loading ShaderLab developer environment..." -ForegroundColor Cyan
$vcvars = Get-VcvarsPath
if (Import-VcvarsEnv -VcvarsPath $vcvars) {
    Write-Host "MSVC environment loaded." -ForegroundColor Green
}

Add-PathEntriesFromCfg -PathFile $ConfigPath
$resolvedCrinkler = Resolve-CrinklerPath
if ($resolvedCrinkler) {
    $env:SHADERLAB_CRINKLER = $resolvedCrinkler
    Write-Host "Crinkler path: $resolvedCrinkler" -ForegroundColor Green
} else {
    Write-Host "Crinkler not found automatically. Set SHADERLAB_CRINKLER to crinkler.exe or its folder if needed." -ForegroundColor Yellow
}
Write-Host "Done." -ForegroundColor Cyan
