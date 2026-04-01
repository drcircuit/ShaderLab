# Building ShaderLab

This document is the source of truth for local builds and packaged outputs.

## Prerequisites

- Windows 10/11
- Visual Studio 2022 with C++ Desktop workload
- CMake and Ninja (available in typical VS dev setups)
- Inno Setup 6 (optional, required to build the `.exe` installer package. If not installed, a portable `.zip` is generated instead.)
- Third-party dependencies present under `third_party/` (see `third_party/README.md`)

## Recommended Workflow (VS Code Tasks)

From VS Code, use the workspace tasks:

- `Build: Debug (Fast, No Installer)`
- `Build: Release (Package Installer or Portable Zip)`
- `Reconfigure: Debug and Release CMake`
- `Run: Debug Editor (With Dev Env)`
- `Layering Check`
- `Startup Relaunch Validation`
- `M6 Prebuilt Packaging Spike`

These tasks automatically load `tools/dev_env.ps1` before configuring/building/running.

Build task selection behavior:

- `Ctrl+Shift+B` opens the build task picker (Debug or Release).
- No single build task is forced as default in workspace settings.

## Command-Line Workflow

From repository root in PowerShell:

```powershell
.\tools\dev_env.ps1
cmd /c .\.vscode\build-debug.bat
```

For release:

```powershell
.\tools\dev_env.ps1
cmd /c .\.vscode\build-release.bat
```

Release builds now also produce a setup artifact automatically:

- Installer script: `tools/build_installer.ps1`
- Output folder: `artifacts/`
- Preferred output: Inno Setup installer (`ShaderLabSetup-x64-...exe`)
- Fallback output: portable zip (`ShaderLabSetup-x64-...-portable.zip`) when Inno Setup is unavailable
- Inno Setup architecture directives use modern identifiers (`x64compatible`) to avoid deprecated `x64` warnings.
- End-user docs from `docs/enduser/` are staged into `build/bin/docs/enduser/` and included in installer/portable artifacts.

## App Metadata and Versioning (Windows)

ShaderLab uses a single metadata source for versioning and Windows file properties:

- Source of truth: `metadata/app_metadata.json`
- CMake consumes it to set `project(VERSION ...)`
- CMake generates:
  - `generated/include/ShaderLab/Generated/AppMetadata.h` (for C++ access)
  - per-target VERSIONINFO resources (embedded in `.exe`/`.scr`)
- Release workflow and installer read the same JSON metadata

How to update app identity/version safely:

1. Edit `metadata/app_metadata.json`
   - `version` must be semantic: `MAJOR.MINOR.PATCH`
   - keep `productName`, `companyName`, `publisher`, and `description` in sync with your release intent
2. Reconfigure/build (`Reconfigure CMake` or normal build task)
3. Verify Windows file properties on built binaries (`Details` tab)
4. Run packaging/release steps normally

Compatibility note:

- `include/ShaderLab/Constants.h` remains as a compatibility wrapper for existing code and now resolves version from generated metadata.

VC++ runtime bundling for installer builds:

- The installer script attempts to locate and bundle `vc_redist.x64.exe`
- If found, installer runs it silently during setup (`/install /quiet /norestart`)
- If not found, installer is still generated, but runtime installation is skipped

## Build Modes and Runtime Targets

ShaderLab supports two practical runtime paths:

- Tiny preset range (`Micro 1K` through `Micro 64K`):
  - Uses `MicroPlayer`
  - Targets x86 (Win32)
- Open/free preset (`None`):
  - Uses full runtime player
  - Targets x64

The build pipeline logs selected architecture and runtime path in build output.

## Target Graph (Current)

- `ShaderLabCoreApi`: shared runtime/engine APIs
- `ShaderLabDevKit`: player runtime implementation
- `ShaderLabDevKitBuildTools`: build/export orchestration
- `ShaderLabEditorLib`: editor UI/orchestration

Executable linkage:

- `ShaderLabIIDE` -> `ShaderLabEditorLib + ShaderLabDevKitBuildTools + ShaderLabCoreApi`
- `ShaderLabBuildCli` -> `ShaderLabDevKitBuildTools + ShaderLabCoreApi`
- `ShaderLabPlayer`/`ShaderLabScreenSaver` -> entrypoint + `ShaderLabDevKit + ShaderLabCoreApi`

Layering enforcement:

- Configure-time CMake checks fail fast on forbidden source coupling (for example runtime targets pulling UI or build/export sources).
- `tools/check.ps1` runs a dedicated configure pass to validate these checks locally.

## Validation Gates (Local + CI)

Primary validation entry point:

- `tools/check.ps1`

Current gates in `tools/check.ps1`:

- documentation integrity (`tools/check_docs.ps1`)
- CMake layering assertions (configure-time)
- editor include-boundary guard (prevents editor/UI runtime-internal includes)
- runtime fullscreen policy guard (blocks exclusive-fullscreen APIs in runtime sources)
- micro packaging contract guard (`track.bin` compact payload generation signal)
- M6 prebuilt packaging spike guard (restricted packaged zip layout + manifest contract validation)

M6 validator details:

- Script: `tools/validate_devkit_prebuilt_spike.ps1`
- Produces a reference packaged artifact with compact sync payload:
  - `--target packaged --restricted-compact-track`
- Validates extracted package contract:
  - runtime executable at package root
  - `project.json`
  - `assets/track.bin`
  - precompiled shaders under `assets/shaders/*.cso`
  - manifest path integrity and precompiled-path resolution
- By default, also runs startup/relaunch matrix (`tools/validate_startup_relaunch_matrix.ps1`) as the M6 spike launch checkpoint.

CI workflows:

- `.github/workflows/release.yml`
  - runs `tools/check.ps1` as a release gate after Release build
  - check gate is non-blocking (warning-only on failure)

Manual/Local validation:

- Run `tools/check.ps1` for routine guardrails.
- Run `tools/validate_devkit_prebuilt_spike.ps1` for full M6 spike validation.

## Size-Sensitive Logging Flags

The following CMake options are default OFF and intended only for diagnostics:

- `SHADERLAB_RUNTIME_DEBUG_LOG`
- `SHADERLAB_COMPACT_TRACK_DEBUG`

These are exposed in the editor Build Settings and should remain OFF for tiny/release size runs.

## Crinkler Notes

- Crinkler integration depends on a valid MSVC environment.
- If toolset pinning fails, the build pipeline retries with default toolset resolution.
- Validate your local `SHADERLAB_CRINKLER` path in environment setup when using crinkled outputs.

### Optional: Bundle Windows SDK (deterministic micro builds)

For size-sensitive micro/crinkled builds, you can bundle a known-good SDK slice directly in the repo.

Create/update bundle:

```powershell
.\tools\bundle_windows_sdk.ps1
```

Default bundle location:

- `third_party/windows_sdk_bundle/`

When present, `BuildPipeline` prefers this bundled SDK for include/lib/rc tool resolution during vcvars-based builds.

## Build Outputs

Typical outputs:

- Editor: `build/bin/ShaderLabIIDE.exe`
- Runtime/player outputs: under active build directory `bin/`
- Packaged assets: `build_selfcontained_pack/` when packaging is used

## Clean Solution Export Format

For all build modes/targets, the exported clean solution folder uses linked shader sources:

- `project.json` stores shader references (`codePath` and `code` as `@file:<relative-path>`)
- Scene and post-FX HLSL files are written under `assets/shaders/hlsl/`
- Shader source is not embedded inline in clean-solution `project.json`

This keeps the clean solution recompile-friendly when you edit and rebuild shaders directly from source files.

## Troubleshooting

- Missing compiler headers (`cstdint`, `string`, `excpt.h`) usually means MSVC environment is not loaded correctly.
- Re-run environment setup in the same shell: `.\tools\dev_env.ps1`.
- Reconfigure CMake if switching major toolchain/runtime options.

## Related Docs

- `docs/QUICKSTART.md`
- `docs/ARCHITECTURE.md`
- `docs/STRUCTURE.md`
- `third_party/README.md`
