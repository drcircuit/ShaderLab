# ShaderLab Project Structure

High-level layout of active directories and ownership.

## Repository Layout

```text
ShaderLab/
├── include/ShaderLab/            # Public headers by subsystem/layer
│   ├── Core/                     # Core API surface (runtime/engine-facing)
│   ├── DevKit/                   # Build/export API surface (Dev Kit-owned)
│   └── Runtime/                  # Runtime player policy API surface
├── src/
│   ├── app/                      # Entry points (editor/runtime players)
│   ├── audio/                    # Audio playback and beat timing
│   ├── core/                     # Serializer/package + (current location of DevKit tooling impl)
│   ├── editor/                   # Editor-side logic and orchestration
│   ├── graphics/                 # D3D12 device, swapchain, render infra
│   ├── runtime/                  # Runtime-side systems and playback
│   ├── shader/                   # DXC integration and shader services
│   └── ui/                       # ImGui-based editor UI
├── creative/                     # Example shaders/projects and creative assets
├── docs/                         # Canonical documentation
│   └── archive/                  # Historical/deprecated notes
├── third_party/                  # External dependencies
├── tools/                        # Setup/build/check/sign utility scripts
└── templates/                    # Standalone/runtime build templates
```

## Build Artifacts (Generated)

- `build/`
- `build_player/`
- `build_selfcontained/`
- `build_selfcontained_ninja/`
- `build_selfcontained_pack/`

These are generated outputs and should not be treated as source directories.

## Runtime Target Policy

- Tiny presets use `MicroPlayer` and x86 output.
- Open/free preset uses full runtime and x64 output.

## Target Ownership (Current)

- `ShaderLabCoreApi`: `src/{graphics,audio,shader}` + core serializer/package APIs
- `ShaderLabDevKit`: player runtime implementation (`src/app/runtime/*`)
- `ShaderLabDevKitBuildTools`: build/export tooling APIs and implementation
- `ShaderLabEditorLib`: editor UI and editor-specific orchestration

## Editor Include Boundary

Editor and UI source trees (`src/app/ShaderLabMain/*`, `src/ui/*`, `include/ShaderLab/UI/*`) should not include runtime-internal app/runtime headers.

- Allowed include surfaces:
	- `ShaderLab/DevKit/*`
	- `ShaderLab/Core/*` (core runtime/engine APIs)
	- `ShaderLab/Graphics/*`, `ShaderLab/Audio/*`, `ShaderLab/Shader/*`
- Forbidden from editor/UI source trees:
	- `ShaderLab/App/PlayerApp.h`
	- `ShaderLab/App/DemoPlayer.h`
	- `ShaderLab/App/Runtime*`
	- `ShaderLab/Runtime/*`
	- direct `src/app/runtime/*` includes

Validation:
- `tools/check.ps1` enforces this with the **Editor Include Boundary** check.
- Target-level layering assertions in `CMakeLists.txt` enforce source ownership boundaries.

## Canonical Docs

- `docs/README.md`
- `docs/QUICKSTART.md`
- `docs/BUILD.md`
- `docs/ARCHITECTURE.md`
- `docs/CONTRIBUTING.md`
