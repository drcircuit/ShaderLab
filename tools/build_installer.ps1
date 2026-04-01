param(
    [string]$AppName = "ShaderLab",
    [string]$BuildBin = "",
    [string]$OutputDir = "",
    [string]$Version = ""
)

$ErrorActionPreference = "Stop"

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
if ([string]::IsNullOrWhiteSpace($BuildBin)) {
    $BuildBin = Join-Path $repoRoot "build\bin"
}
if ([string]::IsNullOrWhiteSpace($OutputDir)) {
    $OutputDir = Join-Path $repoRoot "artifacts"
} elseif (-not [System.IO.Path]::IsPathRooted($OutputDir)) {
    $OutputDir = Join-Path $repoRoot $OutputDir
}

if (-not (Test-Path $BuildBin)) {
    throw "Build output folder not found: $BuildBin"
}

$editorExe = Join-Path $BuildBin "ShaderLabIIDE.exe"
if (-not (Test-Path $editorExe)) {
    throw "Required runtime not found: $editorExe"
}

$playerExe = Join-Path $BuildBin "ShaderLabPlayer.exe"
if (-not (Test-Path $playerExe)) {
    throw "Required runtime not found: $playerExe"
}

$editorInfo = Get-Item $editorExe
$playerInfo = Get-Item $playerExe
Write-Host ("Using editor binary: {0} (size={1} bytes, mtime={2})" -f $editorInfo.FullName, $editorInfo.Length, $editorInfo.LastWriteTime.ToString("s")) -ForegroundColor DarkCyan
Write-Host ("Using player binary: {0} (size={1} bytes, mtime={2})" -f $playerInfo.FullName, $playerInfo.Length, $playerInfo.LastWriteTime.ToString("s")) -ForegroundColor DarkCyan

if ([string]::IsNullOrWhiteSpace($Version)) {
    $metadataPath = Join-Path $repoRoot "metadata\app_metadata.json"
    if (-not (Test-Path $metadataPath)) {
        throw "Version source missing: $metadataPath"
    }

    $metadata = Get-Content -Path $metadataPath -Raw | ConvertFrom-Json
    $Version = [string]$metadata.version
    if ([string]::IsNullOrWhiteSpace($Version) -or ($Version -notmatch '^[0-9]+\.[0-9]+\.[0-9]+$')) {
        throw "Unable to parse semantic version (x.y.z) from $metadataPath"
    }
}

$timestamp = Get-Date -Format "yyyyMMdd_HHmmss"
$stageRoot = Join-Path $repoRoot "build\installer_stage"
$stageApp = Join-Path $stageRoot "app"
$stagePrereq = Join-Path $stageRoot "prereqs"

New-Item -ItemType Directory -Force -Path $stageApp | Out-Null
New-Item -ItemType Directory -Force -Path $stagePrereq | Out-Null
New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null

Get-ChildItem -Path $stageRoot -Force | Remove-Item -Recurse -Force
New-Item -ItemType Directory -Force -Path $stageApp | Out-Null
New-Item -ItemType Directory -Force -Path $stagePrereq | Out-Null

Write-Host "Staging application files from $BuildBin" -ForegroundColor Cyan
Copy-Item -Path (Join-Path $BuildBin "*") -Destination $stageApp -Recurse -Force

$extraToCopy = @(
    "templates",
    "editor_assets",
    "docs\\enduser",
    "LICENSE-COMMUNITY.md",
    "LICENSE-COMMERCIAL.md",
    "README.md"
)
foreach ($item in $extraToCopy) {
    $src = Join-Path $repoRoot $item
    if (Test-Path $src) {
        Copy-Item -Path $src -Destination $stageApp -Recurse -Force
    }
}

# Remove user-local runtime state so installs are always clean/fresh.
$localStatePatterns = @(
    "imgui.ini",
    "*.shaderlab.user.json",
    "ui_settings.json",
    "snippets.json"
)
foreach ($pattern in $localStatePatterns) {
    Get-ChildItem -Path $stageApp -Filter $pattern -File -Recurse -ErrorAction SilentlyContinue |
        Remove-Item -Force -ErrorAction SilentlyContinue
}
Get-ChildItem -Path $stageApp -Directory -Filter ".shaderlab" -Recurse -ErrorAction SilentlyContinue |
    Remove-Item -Recurse -Force -ErrorAction SilentlyContinue

$stageDevKitCmake = Join-Path $stageApp "dev_kit\CMakeLists.txt"
if (-not (Test-Path $stageDevKitCmake)) {
    Write-Host "dev_kit not found in build output; staging fallback development kit sources." -ForegroundColor Yellow
    $devKitRoot = Join-Path $stageApp "dev_kit"
    New-Item -ItemType Directory -Force -Path $devKitRoot | Out-Null

    $devKitEntries = @(
        @{ Source = "include"; Destination = "dev_kit\include" },
        @{ Source = "src"; Destination = "dev_kit\src" },
        @{ Source = "third_party"; Destination = "dev_kit\third_party" }
    )

    foreach ($entry in $devKitEntries) {
        $src = Join-Path $repoRoot $entry.Source
        $dst = Join-Path $stageApp $entry.Destination
        if (-not (Test-Path $src)) {
            throw "Missing required source for installer dev_kit fallback: $src"
        }
        Copy-Item -Path $src -Destination $dst -Recurse -Force
    }

    $templateSrc = Join-Path $repoRoot "templates\Standalone_CMakeLists.txt"
    if (-not (Test-Path $templateSrc)) {
        throw "Missing required Standalone CMake template: $templateSrc"
    }
    Copy-Item -Path $templateSrc -Destination $stageDevKitCmake -Force
}

$openFontIconsSrc = Join-Path $repoRoot "third_party\OpenFontIcons"
if (Test-Path $openFontIconsSrc) {
    $thirdPartyDest = Join-Path $stageApp "third_party"
    New-Item -ItemType Directory -Force -Path $thirdPartyDest | Out-Null
    Copy-Item -Path $openFontIconsSrc -Destination $thirdPartyDest -Recurse -Force
}

function Resolve-VcRedist {
    if ($env:VCToolsRedistDir) {
        $candidate = Join-Path $env:VCToolsRedistDir "vc_redist.x64.exe"
        if (Test-Path $candidate) { return $candidate }
    }

    $vsWhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path $vsWhere) {
        $installPath = & $vsWhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath 2>$null
        if ($LASTEXITCODE -eq 0 -and -not [string]::IsNullOrWhiteSpace($installPath)) {
            $redistRoot = Join-Path $installPath "VC\Redist\MSVC"
            if (Test-Path $redistRoot) {
                $latest = Get-ChildItem -Path $redistRoot -Directory | Sort-Object Name -Descending | Select-Object -First 1
                if ($latest) {
                    $candidate = Join-Path $latest.FullName "vc_redist.x64.exe"
                    if (Test-Path $candidate) { return $candidate }
                }
            }
        }
    }

    $fallbackRoots = @(
        "C:\Program Files\Microsoft Visual Studio\2022\BuildTools\VC\Redist\MSVC",
        "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Redist\MSVC",
        "C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Redist\MSVC",
        "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Redist\MSVC"
    )
    foreach ($root in $fallbackRoots) {
        if (-not (Test-Path $root)) { continue }
        $latest = Get-ChildItem -Path $root -Directory | Sort-Object Name -Descending | Select-Object -First 1
        if ($latest) {
            $candidate = Join-Path $latest.FullName "vc_redist.x64.exe"
            if (Test-Path $candidate) { return $candidate }
        }
    }

    return $null
}

$vcRedistSource = Resolve-VcRedist
$vcRedistStaged = $null
if ($vcRedistSource) {
    $vcRedistStaged = Join-Path $stagePrereq "vc_redist.x64.exe"
    Copy-Item -Path $vcRedistSource -Destination $vcRedistStaged -Force
    Write-Host "Bundled VC++ runtime: $vcRedistSource" -ForegroundColor Green
} else {
    Write-Host "VC++ redistributable not found. Installer will be generated without bundled vc_redist.x64.exe." -ForegroundColor Yellow
}

function Resolve-Iscc {
    $command = Get-Command iscc.exe -ErrorAction SilentlyContinue
    if ($command) { return $command.Source }

    $candidates = @(
        "C:\Program Files (x86)\Inno Setup 6\ISCC.exe",
        "C:\Program Files\Inno Setup 6\ISCC.exe"
    )
    foreach ($path in $candidates) {
        if (Test-Path $path) { return $path }
    }
    return $null
}

$iscc = Resolve-Iscc

$issPath = Join-Path $stageRoot "shaderlab_installer.iss"

function Escape-InnoPath([string]$value) {
    return $value.Replace("\", "\\")
}

$stageAppEsc = Escape-InnoPath $stageApp
$outputDirEsc = Escape-InnoPath $OutputDir
$repoRootEsc = Escape-InnoPath $repoRoot

$issLines = @(
    ('#define MyAppName "{0}"' -f $AppName),
    ('#define MyAppVersion "{0}"' -f $Version),
    '#define MyAppPublisher "ShaderLab"',
    '#define MyAppExeName "ShaderLabIIDE.exe"',
    ('#define SourceRoot "{0}"' -f $stageAppEsc),
    '',
    '[Setup]',
    'AppId={{A3FA0150-A7EA-4A72-B7EA-6AE309D375D5}',
    'AppName={#MyAppName}',
    'AppVersion={#MyAppVersion}',
    'AppPublisher={#MyAppPublisher}',
    'DefaultDirName={autopf}\{#MyAppName}',
    'DefaultGroupName={#MyAppName}',
    'DisableProgramGroupPage=yes',
    ('OutputBaseFilename=ShaderLabSetup-x64-{0}-{1}' -f $Version, $timestamp),
    ('OutputDir={0}' -f $outputDirEsc),
    'Compression=lzma2',
    'SolidCompression=yes',
    'ArchitecturesAllowed=x64compatible',
    'ArchitecturesInstallIn64BitMode=x64compatible',
    'SetupIconFile={#SourceRoot}\\editor_assets\\shaderlab.ico.ico',
    'UninstallDisplayIcon={app}\\editor_assets\\shaderlab.ico.ico',
    ('LicenseFile={0}\LICENSE-COMMUNITY.md' -f $repoRootEsc),
    '',
    '[Languages]',
    'Name: "english"; MessagesFile: "compiler:Default.isl"',
    '',
    '[Tasks]',
    'Name: "desktopicon"; Description: "Create a desktop icon"; GroupDescription: "Additional icons:"',
    '',
    '[Dirs]',
    'Name: "{code:GetWorkspaceFolder}"',
    'Name: "{code:GetWorkspaceProjects}"',
    'Name: "{code:GetWorkspaceSnippets}"',
    'Name: "{code:GetWorkspacePostFx}"',
    '',
    '[Registry]',
    'Root: HKCU; Subkey: "Software\ShaderLab"; ValueType: string; ValueName: "WorkspaceFolder"; ValueData: "{code:GetWorkspaceFolder}"; Flags: uninsdeletevalue',
    '',
    '[Files]',
    'Source: "{#SourceRoot}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs'
)

if ($vcRedistStaged) {
    $vcRedistEsc = Escape-InnoPath $vcRedistStaged
    $issLines += ('Source: "{0}"; DestDir: "{{tmp}}"; Flags: deleteafterinstall' -f $vcRedistEsc)
}

$issLines += @(
    '',
    '[Icons]',
    'Name: "{autoprograms}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; IconFilename: "{app}\editor_assets\shaderlab.ico.ico"; AppUserModelID: "ShaderLab.Editor"',
    'Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon; IconFilename: "{app}\editor_assets\shaderlab.ico.ico"; AppUserModelID: "ShaderLab.Editor"',
    '',
    '[Run]'
)

if ($vcRedistStaged) {
    $issLines += 'Filename: "{tmp}\vc_redist.x64.exe"; Parameters: "/install /quiet /norestart"; StatusMsg: "Installing Microsoft Visual C++ Runtime..."; Flags: runhidden waituntilterminated'
}

$issLines += 'Filename: "{app}\{#MyAppExeName}"; Description: "Launch {#MyAppName}"; Flags: nowait postinstall skipifsilent'

$issLines += @(
    '',
    '[Code]',
    'var',
    '  WorkspacePage: TInputDirWizardPage;',
    '',
    'procedure InitializeWizard();',
    'var',
    '  DefaultWorkspace: string;',
    'begin',
    '  DefaultWorkspace := ExpandConstant(''{%USERPROFILE}\ShaderLabs'');',
    '  WorkspacePage := CreateInputDirPage(',
    '    wpSelectDir,',
    '    ''Shader Workspace Folder'',',
    '    ''Choose where ShaderLab stores your workspace files.'',',
    '    ''ShaderLab will create and use subfolders: projects, snippets, and postfx.'',',
    '    False,',
    '    '''');',
    '  WorkspacePage.Add(''Workspace folder:'');',
    '  WorkspacePage.Values[0] := DefaultWorkspace;',
    'end;',
    '',
    'function GetWorkspaceFolder(Param: string): string;',
    'begin',
    '  if Assigned(WorkspacePage) then',
    '    Result := WorkspacePage.Values[0]',
    '  else',
    '    Result := ExpandConstant(''{%USERPROFILE}\ShaderLabs'');',
    'end;',
    '',
    'function GetWorkspaceProjects(Param: string): string;',
    'begin',
    '  Result := AddBackslash(GetWorkspaceFolder(Param)) + ''projects'';',
    'end;',
    '',
    'function GetWorkspaceSnippets(Param: string): string;',
    'begin',
    '  Result := AddBackslash(GetWorkspaceFolder(Param)) + ''snippets'';',
    'end;',
    '',
    'function GetWorkspacePostFx(Param: string): string;',
    'begin',
    '  Result := AddBackslash(GetWorkspaceFolder(Param)) + ''postfx'';',
    'end;'
)

Set-Content -Path $issPath -Value $issLines -Encoding UTF8

if ($iscc) {
    Write-Host "Building installer with Inno Setup: $iscc" -ForegroundColor Cyan
    & $iscc $issPath
    if ($LASTEXITCODE -ne 0) {
        throw "Inno Setup build failed with exit code $LASTEXITCODE"
    }
    Write-Host "Installer created in: $OutputDir" -ForegroundColor Green
    exit 0
}

Write-Host "Inno Setup (ISCC.exe) not found. Creating fallback portable artifact." -ForegroundColor Yellow
$zipName = "ShaderLabSetup-x64-$Version-$timestamp-portable.zip"
$zipPath = Join-Path $OutputDir $zipName
if (Test-Path $zipPath) {
    Remove-Item -Path $zipPath -Force
}
Compress-Archive -Path (Join-Path $stageApp "*") -DestinationPath $zipPath -Force
Write-Host "Portable artifact created: $zipPath" -ForegroundColor Green
