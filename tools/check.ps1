# Check if environment is ready to build

Write-Host "ShaderLab Build Environment Check" -ForegroundColor Cyan
Write-Host "===================================" -ForegroundColor Cyan
Write-Host ""

$allReady = $true
$docsReady = $true
$microContractReady = $true
$tinyCrinklerSmokeReady = $true
$m6PrebuiltReady = $true
$editorIncludeReady = $true
$fullscreenPolicyReady = $true
$uiStyleGuardReady = $true
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path

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

    $fallbacks = @(
        "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat",
        "C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat",
        "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvars64.bat",
        "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
    )

    foreach ($candidate in $fallbacks) {
        if (Test-Path $candidate) {
            return $candidate
        }
    }

    return $null
}

# Check CMake
Write-Host "CMake: " -NoNewline
if (Get-Command cmake -ErrorAction SilentlyContinue) {
    Write-Host "Installed" -ForegroundColor Green
} else {
    Write-Host "NOT FOUND" -ForegroundColor Red
    $allReady = $false
}

# Check Ninja
Write-Host "Ninja: " -NoNewline
if (Get-Command ninja -ErrorAction SilentlyContinue) {
    Write-Host "Installed" -ForegroundColor Green
} else {
    Write-Host "NOT FOUND" -ForegroundColor Red
    $allReady = $false
}

# Check Visual Studio
Write-Host "Visual Studio 2022: " -NoNewline
$vsPaths = @(
    "C:\Program Files\Microsoft Visual Studio\2022\Community",
    "C:\Program Files\Microsoft Visual Studio\2022\Professional",
    "C:\Program Files\Microsoft Visual Studio\2022\Enterprise",
    "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools"
)

$vsFound = $false
foreach ($path in $vsPaths) {
    $vcvars = Join-Path $path "VC\Auxiliary\Build\vcvars64.bat"
    if (Test-Path $vcvars) {
        Write-Host "Installed at $path" -ForegroundColor Green
        $vsFound = $true
        break
    }
}

if (-not $vsFound) {
    Write-Host "NOT FOUND or STILL INSTALLING" -ForegroundColor Yellow
    Write-Host "  Visual Studio may still be installing..." -ForegroundColor Yellow
    $allReady = $false
}

# Check Dependencies
Write-Host ""
Write-Host "Dependencies:" -ForegroundColor Cyan

$deps = @{
    "Dear ImGui" = "third_party\imgui\imgui.h"
    "miniaudio" = "third_party\miniaudio\miniaudio.h"
    "nlohmann/json" = "third_party\json\include\nlohmann\json.hpp"
    "stb_image" = "third_party\stb\stb_image.h"
}

foreach ($dep in $deps.GetEnumerator()) {
    Write-Host "  $($dep.Key): " -NoNewline
    if (Test-Path $dep.Value) {
        Write-Host "OK" -ForegroundColor Green
    } else {
        Write-Host "MISSING" -ForegroundColor Red
        $allReady = $false
    }
}

Write-Host ""
Write-Host "Documentation:" -ForegroundColor Cyan
$docsCheckScript = Join-Path $PSScriptRoot "check_docs.ps1"
if (Test-Path $docsCheckScript) {
    & $docsCheckScript
    if ($LASTEXITCODE -eq 0) {
        Write-Host "  Docs check: OK" -ForegroundColor Green
    } else {
        Write-Host "  Docs check: FAILED" -ForegroundColor Red
        $docsReady = $false
    }
} else {
    Write-Host "  Docs check script missing" -ForegroundColor Yellow
    $docsReady = $false
}

Write-Host ""
Write-Host "Layering Guards:" -ForegroundColor Cyan
$layeringReady = $true
$layeringBuildDir = "build_layering_check"

if ($allReady) {
    try {
        if (Test-Path $layeringBuildDir) {
            Remove-Item $layeringBuildDir -Recurse -Force -ErrorAction SilentlyContinue
        }

        $vcvars = Get-VcvarsPath
        if (-not $vcvars) {
            throw "Unable to locate vcvars64.bat for compiler environment setup."
        }

        Write-Host "  Running CMake configure (layering assertions)..." -ForegroundColor Gray
        $layeringCmd = "`"$vcvars`" && cmake -S . -B $layeringBuildDir -G Ninja -DCMAKE_BUILD_TYPE=Debug"
        $cmakeOut = cmd /c $layeringCmd 2>&1
        if ($LASTEXITCODE -eq 0) {
            Write-Host "  Layering checks: OK" -ForegroundColor Green
        } else {
            Write-Host "  Layering checks: FAILED" -ForegroundColor Red
            Write-Host "  CMake output:" -ForegroundColor Yellow
            $cmakeOut | ForEach-Object { Write-Host "    $_" }
            $layeringReady = $false
        }
    }
    catch {
        Write-Host "  Layering checks: FAILED" -ForegroundColor Red
        Write-Host "  Error: $($_.Exception.Message)" -ForegroundColor Yellow
        $layeringReady = $false
    }
    finally {
        if (Test-Path $layeringBuildDir) {
            Remove-Item $layeringBuildDir -Recurse -Force -ErrorAction SilentlyContinue
        }
    }
} else {
    Write-Host "  Skipped (build prerequisites not ready)" -ForegroundColor Yellow
    $layeringReady = $false
}

Write-Host ""
Write-Host "Editor Include Boundary:" -ForegroundColor Cyan
if ($allReady) {
    try {
        $editorRoots = @(
            (Join-Path $repoRoot "src\app\ShaderLabMain"),
            (Join-Path $repoRoot "src\ui"),
            (Join-Path $repoRoot "include\ShaderLab\UI")
        )

        $editorFiles = @()
        foreach ($root in $editorRoots) {
            if (Test-Path $root) {
                $editorFiles += Get-ChildItem -Path $root -Recurse -File -Include *.h,*.hpp,*.cpp
            }
        }

        $violations = @()
        if ($editorFiles.Count -gt 0) {
            $forbiddenPatterns = @(
                '#include\s+"ShaderLab/App/(PlayerApp|DemoPlayer|Runtime[^\"]*)',
                '#include\s+"ShaderLab/Runtime/',
                '#include\s+".*src/app/runtime/'
            )

            foreach ($pattern in $forbiddenPatterns) {
                $violations += Select-String -Path $editorFiles.FullName -Pattern $pattern -CaseSensitive -ErrorAction SilentlyContinue
            }
        }

        if ($violations.Count -eq 0) {
            Write-Host "  Editor include boundary: OK" -ForegroundColor Green
        } else {
            Write-Host "  Editor include boundary: FAILED" -ForegroundColor Red
            Write-Host "  Found editor/runtime include boundary violations:" -ForegroundColor Yellow
            $violations | Select-Object -First 12 | ForEach-Object {
                Write-Host "    $($_.Path):$($_.LineNumber): $($_.Line.Trim())"
            }
            if ($violations.Count -gt 12) {
                Write-Host "    ... and $($violations.Count - 12) more" -ForegroundColor Yellow
            }
            $editorIncludeReady = $false
        }
    }
    catch {
        Write-Host "  Editor include boundary: FAILED" -ForegroundColor Red
        Write-Host "  Error: $($_.Exception.Message)" -ForegroundColor Yellow
        $editorIncludeReady = $false
    }
} else {
    Write-Host "  Skipped (build prerequisites not ready)" -ForegroundColor Yellow
    $editorIncludeReady = $false
}

Write-Host ""
Write-Host "Runtime Fullscreen Policy:" -ForegroundColor Cyan
if ($allReady) {
    try {
        $runtimeRoots = @(
            (Join-Path $repoRoot "src\app\runtime"),
            (Join-Path $repoRoot "src\runtime")
        )

        $runtimeFiles = @()
        foreach ($root in $runtimeRoots) {
            if (Test-Path $root) {
                $runtimeFiles += Get-ChildItem -Path $root -Recurse -File -Include *.h,*.hpp,*.cpp
            }
        }

        $violations = @()
        if ($runtimeFiles.Count -gt 0) {
            $forbiddenPatterns = @(
                'SetFullscreenState\s*\(',
                'ChangeDisplaySettingsEx\s*\(',
                'CDS_FULLSCREEN'
            )

            foreach ($pattern in $forbiddenPatterns) {
                $violations += Select-String -Path $runtimeFiles.FullName -Pattern $pattern -CaseSensitive -ErrorAction SilentlyContinue
            }
        }

        if ($violations.Count -eq 0) {
            Write-Host "  Runtime fullscreen policy: OK" -ForegroundColor Green
        } else {
            Write-Host "  Runtime fullscreen policy: FAILED" -ForegroundColor Red
            Write-Host "  Found exclusive fullscreen API usage in runtime sources:" -ForegroundColor Yellow
            $violations | Select-Object -First 12 | ForEach-Object {
                Write-Host "    $($_.Path):$($_.LineNumber): $($_.Line.Trim())"
            }
            if ($violations.Count -gt 12) {
                Write-Host "    ... and $($violations.Count - 12) more" -ForegroundColor Yellow
            }
            $fullscreenPolicyReady = $false
        }
    }
    catch {
        Write-Host "  Runtime fullscreen policy: FAILED" -ForegroundColor Red
        Write-Host "  Error: $($_.Exception.Message)" -ForegroundColor Yellow
        $fullscreenPolicyReady = $false
    }
} else {
    Write-Host "  Skipped (build prerequisites not ready)" -ForegroundColor Yellow
    $fullscreenPolicyReady = $false
}

Write-Host ""
Write-Host "Micro Packaging Contract:" -ForegroundColor Cyan
$microCheckDir = "build_micro_contract_check"

Write-Host ""
Write-Host "UI Style Guardrails:" -ForegroundColor Cyan
if ($allReady) {
    try {
        $uiRoot = Join-Path $repoRoot "src\ui"
        $uiFiles = @()
        if (Test-Path $uiRoot) {
            $uiFiles = Get-ChildItem -Path $uiRoot -Recurse -File -Include *.h,*.hpp,*.cpp
        }

        $violations = @()
        if ($uiFiles.Count -gt 0) {
            $forbiddenPatterns = @(
                'PushStyleColor\s*\(\s*ImGuiCol_FrameBg\s*,\s*ImVec4\s*\(',
                'PushStyleColor\s*\(\s*ImGuiCol_ChildBg\s*,\s*ImVec4\s*\(',
                'PushStyleColor\s*\(\s*ImGuiCol_PopupBg\s*,\s*ImVec4\s*\(',
                'ImGuiCol_FrameBg\s*\]\s*=\s*ImVec4\s*\(',
                'ImGuiCol_ChildBg\s*\]\s*=\s*ImVec4\s*\(',
                'ImGuiCol_PopupBg\s*\]\s*=\s*ImVec4\s*\('
            )

            foreach ($pattern in $forbiddenPatterns) {
                $violations += Select-String -Path $uiFiles.FullName -Pattern $pattern -CaseSensitive -ErrorAction SilentlyContinue
            }
        }

        if ($violations.Count -eq 0) {
            Write-Host "  UI style guardrails: OK" -ForegroundColor Green
        } else {
            Write-Host "  UI style guardrails: FAILED" -ForegroundColor Red
            Write-Host "  Found hardcoded ImGui color literals for FrameBg/ChildBg/PopupBg in src/ui:" -ForegroundColor Yellow
            $violations | Select-Object -First 12 | ForEach-Object {
                Write-Host "    $($_.Path):$($_.LineNumber): $($_.Line.Trim())"
            }
            if ($violations.Count -gt 12) {
                Write-Host "    ... and $($violations.Count - 12) more" -ForegroundColor Yellow
            }
            $uiStyleGuardReady = $false
        }
    }
    catch {
        Write-Host "  UI style guardrails: FAILED" -ForegroundColor Red
        Write-Host "  Error: $($_.Exception.Message)" -ForegroundColor Yellow
        $uiStyleGuardReady = $false
    }
} else {
    Write-Host "  Skipped (build prerequisites not ready)" -ForegroundColor Yellow
    $uiStyleGuardReady = $false
}

if ($allReady -and $layeringReady) {
    try {
        if (Test-Path $microCheckDir) {
            Remove-Item $microCheckDir -Recurse -Force -ErrorAction SilentlyContinue
        }
        New-Item -ItemType Directory -Path $microCheckDir -Force | Out-Null

        $buildCliCandidates = @(
            (Join-Path $repoRoot "build_debug\bin\ShaderLabBuildCli.exe"),
            (Join-Path $repoRoot "build\bin\ShaderLabBuildCli.exe")
        )
        $buildCliPath = $buildCliCandidates | Where-Object { Test-Path $_ } | Select-Object -First 1

        if (-not $buildCliPath) {
            Write-Host "  Micro packaging contract: FAILED" -ForegroundColor Red
            Write-Host "  ShaderLabBuildCli.exe not found (expected in build_debug/bin or build/bin)." -ForegroundColor Yellow
            $microContractReady = $false
        } else {
            $projectCandidates = @(
                (Join-Path $repoRoot "experiments\cloud_tunnel\project.json")
            )
            $projectPath = $projectCandidates | Where-Object { Test-Path $_ } | Select-Object -First 1

            if (-not $projectPath) {
                Write-Host "  Micro packaging contract: FAILED" -ForegroundColor Red
                Write-Host "  No project.json candidate found for micro contract check." -ForegroundColor Yellow
                $microContractReady = $false
            } else {
                $artifactPath = Join-Path $microCheckDir "artifact_micro_contract.exe"
                $solutionRoot = Join-Path $microCheckDir "root"
                $microLogPath = Join-Path $microCheckDir "log_micro_contract.txt"

                & $buildCliPath --app-root $repoRoot --project $projectPath --output $artifactPath --solution-root $solutionRoot --target micro --mode release --size 64k *> $microLogPath

                if ($LASTEXITCODE -ne 0) {
                    Write-Host "  Micro packaging contract: FAILED" -ForegroundColor Red
                    Write-Host "  BuildCli returned exit code $LASTEXITCODE. See: $microLogPath" -ForegroundColor Yellow
                    $microContractReady = $false
                } else {
                    $contractLine = Select-String -Path $microLogPath -Pattern "MicroPlayer packaging: compact track binary enabled" -SimpleMatch -ErrorAction SilentlyContinue
                    if ($contractLine) {
                        Write-Host "  Micro packaging contract: OK" -ForegroundColor Green
                    } else {
                        Write-Host "  Micro packaging contract: FAILED" -ForegroundColor Red
                        Write-Host "  Expected log signal missing: 'MicroPlayer packaging: compact track binary enabled'" -ForegroundColor Yellow
                        Write-Host "  See: $microLogPath" -ForegroundColor Yellow
                        $microContractReady = $false
                    }
                }
            }
        }
    }
    catch {
        Write-Host "  Micro packaging contract: FAILED" -ForegroundColor Red
        Write-Host "  Error: $($_.Exception.Message)" -ForegroundColor Yellow
        $microContractReady = $false
    }
} else {
    Write-Host "  Skipped (build prerequisites/layering checks not ready)" -ForegroundColor Yellow
    $microContractReady = $false
}

Write-Host ""
Write-Host "M6 Prebuilt Packaging Spike:" -ForegroundColor Cyan
$m6Script = Join-Path $PSScriptRoot "validate_devkit_prebuilt_spike.ps1"

if ($allReady -and $layeringReady -and $microContractReady) {
    try {
        if (-not (Test-Path $m6Script)) {
            Write-Host "  M6 prebuilt spike: FAILED" -ForegroundColor Red
            Write-Host "  Missing script: $m6Script" -ForegroundColor Yellow
            $m6PrebuiltReady = $false
        } else {
            $m6GateDir = Join-Path $repoRoot "build_m6_prebuilt_spike"
            New-Item -ItemType Directory -Path $m6GateDir -Force | Out-Null
            $m6LogPath = Join-Path $m6GateDir "log_m6_gate.txt"
            & powershell -NoProfile -ExecutionPolicy Bypass -File $m6Script -SkipLaunchMatrix *> $m6LogPath

            if ($LASTEXITCODE -ne 0) {
                Write-Host "  M6 prebuilt spike: FAILED" -ForegroundColor Red
                Write-Host "  Validation script returned exit code $LASTEXITCODE. See: $m6LogPath" -ForegroundColor Yellow
                $m6PrebuiltReady = $false
            } else {
                Write-Host "  M6 prebuilt spike: OK" -ForegroundColor Green
            }
        }
    }
    catch {
        Write-Host "  M6 prebuilt spike: FAILED" -ForegroundColor Red
        Write-Host "  Error: $($_.Exception.Message)" -ForegroundColor Yellow
        $m6PrebuiltReady = $false
    }
} else {
    Write-Host "  Skipped (build prerequisites/layering/micro checks not ready)" -ForegroundColor Yellow
    $m6PrebuiltReady = $false
}

Write-Host ""
Write-Host "Tiny Crinkler Smoke:" -ForegroundColor Cyan
$tinyCrinklerScript = Join-Path $PSScriptRoot "validate_tiny_crinkler_smoke.ps1"

if ($allReady) {
    try {
        if (-not (Test-Path $tinyCrinklerScript)) {
            Write-Host "  Tiny Crinkler smoke: FAILED" -ForegroundColor Red
            Write-Host "  Missing script: $tinyCrinklerScript" -ForegroundColor Yellow
            $tinyCrinklerSmokeReady = $false
        } else {
            $tinySmokeLogDir = Join-Path $repoRoot "build_tiny_crinkler_smoke"
            New-Item -ItemType Directory -Path $tinySmokeLogDir -Force | Out-Null
            $tinySmokeLogPath = Join-Path $tinySmokeLogDir "log_tiny_crinkler_smoke.txt"

            & powershell -NoProfile -ExecutionPolicy Bypass -File $tinyCrinklerScript -NoClean *> $tinySmokeLogPath
            if ($LASTEXITCODE -ne 0) {
                Write-Host "  Tiny Crinkler smoke: FAILED" -ForegroundColor Red
                Write-Host "  Validation script returned exit code $LASTEXITCODE. See: $tinySmokeLogPath" -ForegroundColor Yellow
                $tinyCrinklerSmokeReady = $false
            } else {
                Write-Host "  Tiny Crinkler smoke: OK" -ForegroundColor Green
            }
        }
    }
    catch {
        Write-Host "  Tiny Crinkler smoke: FAILED" -ForegroundColor Red
        Write-Host "  Error: $($_.Exception.Message)" -ForegroundColor Yellow
        $tinyCrinklerSmokeReady = $false
    }
} else {
    Write-Host "  Skipped (build prerequisites not ready)" -ForegroundColor Yellow
    $tinyCrinklerSmokeReady = $false
}

Write-Host ""
Write-Host "===================================" -ForegroundColor Cyan

if ($allReady -and $docsReady -and $layeringReady -and $editorIncludeReady -and $fullscreenPolicyReady -and $uiStyleGuardReady -and $microContractReady -and $m6PrebuiltReady -and $tinyCrinklerSmokeReady) {
    Write-Host "Ready to build!" -ForegroundColor Green
    Write-Host ""
    Write-Host "Run: .\tools\build.ps1" -ForegroundColor Cyan
    Write-Host "Integration test: .\tools\integration_test.ps1" -ForegroundColor Cyan
} else {
    Write-Host "Some checks failed" -ForegroundColor Yellow
    Write-Host ""
    Write-Host "If Visual Studio is installing, wait for it to complete," -ForegroundColor White
    Write-Host "then restart your terminal and run this check again." -ForegroundColor White
}

Write-Host ""
