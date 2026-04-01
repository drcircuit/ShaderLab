# ShaderLab

A Windows-native demoscene SDK and realtime shader authoring environment.

## Overview

ShaderLab is a minimalist, high-performance toolkit for creating demoscene productions and visual experiments. Built on Direct3D 12 and DXC, it provides a tracker-inspired interface for authoring shader-driven visuals synchronized to music.

Project documentation is maintained under `docs/`.

### Core Features

- **Realtime Shader Authoring**: Live HLSL compilation via DXC with instant feedback
- **Beat-Synchronized Playback**: BPM-based timeline with quarter-note precision
- **Multi-Pass Rendering**: Pixel buffer passes with post-processing chains
- **Playlist System**: Beat-anchored scene transitions for demo sequencing
- **Tracker Interface**: Demoscene-inspired UI built with Dear ImGui
- **Optimized Output**: Debug mode for editing, release mode for final demos

### Editor UI Design

The editor features a custom futuristic cyberpunk aesthetic:
- **Custom Fonts**: Hacked (headings), Orbitron (UI text), Erbos Draco (numbers)
- **Color Scheme**: Dark blue-black backgrounds with bright cyan/teal accents
- **Sharp Geometry**: Angular, industrial design with no rounded corners
- **High Contrast**: Optimized for extended coding sessions in dark environments

*Note: Custom fonts are editor-only and not included in runtime builds.*

## Technology Stack

- **Language**: C++20
- **Graphics**: Direct3D 12
- **Shaders**: HLSL 6.x with DXC compiler
- **UI**: Dear ImGui
- **Audio**: miniaudio
- **Build**: CMake 3.20+ (Ninja optional)
- **JSON**: nlohmann/json
- **Image Loading**: stb_image

## Architecture

```
src/
├── app/        - Entry points for editor and runtime players
├── audio/      - Audio playback and beat clock
├── core/       - Build pipeline, project IO, runtime export, packing
├── editor/     - Editor domain systems
├── graphics/   - D3D12 abstraction
├── runtime/    - Runtime systems used by players
├── shader/     - DXC wrapper and shader system
└── ui/         - ImGui integration and panels

creative/
├── shaders/    - Example shaders
└── examples/   - Demo projects

docs/           - Documentation
third_party/    - External dependencies
tools/          - Build and utility scripts
```

### Module Breakdown

**Graphics Module** (`src/graphics/`)
- D3D12 device and resource management
- Swapchain and present logic
- Command list abstraction
- Minimal overhead, RAII-based

### Runtime Fullscreen Policy (DX12)

- Player runtimes use borderless windowed fullscreen (`WS_POPUP`) with flip-model swapchain.
- Classic exclusive fullscreen (`SetFullscreenState(TRUE)`) is intentionally disabled.
- `DXGI_MWA_NO_ALT_ENTER` is enabled to prevent legacy mode-switch fullscreen toggles.
- Startup path targets black-first present behavior before showing fullscreen window where applicable.

**Shader Module** (`src/shader/`)
- DXC compiler integration
- Live compilation (debug, unoptimized)
- Build compilation (O3, stripped)
- Diagnostic parsing (file, line, column)

**Audio Module** (`src/audio/`)
- Audio file loading and playback
- Playback time tracking
- Beat clock with BPM sync
- Quarter/eighth/sixteenth note tracking

**UI Module** (`src/ui/`)
- ImGui integration with D3D12
- Demo View: Timeline and playlist
- Scene View: Realtime preview
- Effect View: Shader editor and parameters

## Building

### Host Requirements

- Windows 10/11
- Visual Studio 2022 with `Desktop development with C++`
- MSVC v143 toolset and a Windows 10/11 SDK
- CMake 3.20+
- Ninja
- PowerShell 5.1+

Notes:

- The VS Code tasks and the `.vscode/*.bat` wrappers now locate `vcvars64.bat` via `vswhere`, so Visual Studio does not need to be installed in the old `BuildTools` default path.
- Release packaging uses Inno Setup if it is installed at `C:\Program Files (x86)\Inno Setup 6\ISCC.exe` or `C:\Program Files\Inno Setup 6\ISCC.exe`. If not, the release packaging step falls back to a portable `.zip` artifact under `artifacts/`.

### Third-Party Dependencies

ShaderLab expects its external dependencies to exist directly under `third_party/`.

Some are already vendored in this repository and should remain in place after clone:

- `third_party/ImGuiColorTextEdit/`
- `third_party/OpenFontIcons/`
- `third_party/CMakeLists.txt`

Some are fetched or verified by `tools/setup.ps1`:

- `third_party/miniaudio/miniaudio.h`
- `third_party/json/include/nlohmann/json.hpp`
- `third_party/stb/stb_image.h`

Dear ImGui still requires manual placement if it is missing:

- Source: `https://github.com/ocornut/imgui`
- Recommended branch/archive: `docking`
- Destination: extract the ImGui contents so these files exist:
	- `third_party/imgui/imgui.h`
	- `third_party/imgui/imgui.cpp`
	- `third_party/imgui/imgui_draw.cpp`
	- `third_party/imgui/imgui_tables.cpp`
	- `third_party/imgui/imgui_widgets.cpp`
	- `third_party/imgui/imgui_demo.cpp`
	- `third_party/imgui/backends/imgui_impl_win32.cpp`
	- `third_party/imgui/backends/imgui_impl_dx12.cpp`

Optional tooling:

- `third_party/Crinkler.exe` or `SHADERLAB_CRINKLER=<path>` for tiny/crinkled builds
- Inno Setup 6 for `.exe` installer generation instead of the portable `.zip` fallback

Expected `third_party/` layout:

```text
third_party/
├── CMakeLists.txt
├── Crinkler.exe                       # optional
├── imgui/
│   ├── imgui.h
│   ├── imgui.cpp
│   ├── imgui_draw.cpp
│   ├── imgui_tables.cpp
│   ├── imgui_widgets.cpp
│   ├── imgui_demo.cpp
│   └── backends/
│       ├── imgui_impl_win32.cpp
│       └── imgui_impl_dx12.cpp
├── ImGuiColorTextEdit/
├── json/
│   └── include/
│       └── nlohmann/
│           └── json.hpp
├── miniaudio/
│   └── miniaudio.h
├── OpenFontIcons/
│   ├── LICENSE
│   └── OpenFontIcons.ttf
└── stb/
		└── stb_image.h
```

### DXC Runtime

The editor and self-contained build flow expect `dxcompiler.dll` and `dxil.dll` at runtime.

- CMake tries to copy them automatically from the newest installed Windows SDK `bin\<version>\x64` folder.
- If that auto-detect fails, set `SHADERLAB_DXC_BIN_DIR` to the folder containing both DLLs when configuring CMake.
- If the editor launches without DXC, shader compilation and self-contained export will fail even if the C++ build succeeded.

### First-Time Setup

From the repository root:

```powershell
git clone https://github.com/drcircuit/ShaderLabDX12.git
cd ShaderLabDX12

# Fetch header-only dependencies and verify local toolchain visibility.
.\tools\setup.ps1

# Load the MSVC environment into the current shell.
.\tools\dev_env.ps1
```

If `third_party/imgui/` is missing after that, download Dear ImGui and extract it into `third_party/imgui/` before building.

### Build Paths

Recommended from VS Code:

1. Open the workspace root.
2. Run `Ctrl+Shift+B`.
3. Choose one of these tasks:
	 - `Build ShaderLab (Debug, No Installer)`
	 - `Build ShaderLab (Release + Installer)`
	 - `Build ShaderLab (Release, No Installer)`

Direct batch wrappers from the repository root:

```powershell
cmd /c .\.vscode\build-debug.bat
cmd /c .\.vscode\build-release.bat
cmd /c .\.vscode\build-release-no-installer.bat
cmd /c .\.vscode\reconfigure.bat
```

Equivalent PowerShell flow if you want the environment loaded first:

```powershell
.\tools\dev_env.ps1
cmd /c .\.vscode\build-debug.bat
```

Manual CMake flow:

```powershell
.\tools\dev_env.ps1
cmake -S . -B build_debug -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build_debug --config Debug --clean-first --target ShaderLabIIDE ShaderLabBuildCli ShaderLabPlayer ShaderLabScreenSaver
```

Release packaging flow:

```powershell
.\tools\dev_env.ps1
cmake -S . -B build_release -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build_release --config Release --clean-first --target ShaderLabIIDE ShaderLabBuildCli ShaderLabPlayer ShaderLabScreenSaver
powershell -ExecutionPolicy Bypass -File .\tools\stage_enduser_docs.ps1 -BuildBin .\build_release\bin
powershell -ExecutionPolicy Bypass -File .\tools\sign_build.ps1 -ExePath .\build_release\bin\ShaderLabIIDE.exe
powershell -ExecutionPolicy Bypass -File .\tools\build_installer.ps1 -BuildBin .\build_release\bin
```

Build outputs:

- Debug editor: `build_debug\bin\ShaderLabIIDE.exe`
- Release editor: `build_release\bin\ShaderLabIIDE.exe`
- Packaged artifacts: `artifacts\`

### Known Build Failure Causes

- `The system cannot find the path specified.` at the top of the old `.vscode` batch scripts meant the wrapper was calling a hardcoded Visual Studio Build Tools path that did not exist on the machine.
- `The process cannot access the file because it is being used by another process.` means a previous `ShaderLabIIDE.exe`, `ShaderLabBuildCli.exe`, or another process still has files in `build_debug/` or `build_release/` open. Close running binaries before rebuilding.
- `DXC runtime DLLs not found` means `dxcompiler.dll` and `dxil.dll` were not found in the Windows SDK and `SHADERLAB_DXC_BIN_DIR` was not set.
- `Inno Setup (ISCC.exe) not found` is not fatal for release packaging; the scripts generate a portable `.zip` instead.

Third-party dependency details are also listed in [third_party/README.md](third_party/README.md).

For fuller setup and packaging flows, see:
- [docs/README.md](docs/README.md)
- [docs/QUICKSTART.md](docs/QUICKSTART.md)
- [docs/BUILD.md](docs/BUILD.md)

### Build Configurations

- **Debug**: Live shader compilation, validation layers, full diagnostics
- **Release**: Optimized shaders, minimal overhead for final demo output

## Usage

1. **Launch Editor**: Run `build\bin\ShaderLabIIDE.exe` from the repo root
2. **Load Audio**: Import a music track (WAV, MP3, OGG)
3. **Set BPM**: Configure tempo for beat synchronization
4. **Create Effect**: Write HLSL pixel shaders in the Effect View
5. **Arrange Timeline**: Build a demo sequence in the Demo View
6. **Export**: Compile optimized standalone executable

## Licensing

ShaderLab uses a dual-license model:

- **Engine/Editor Code**: Custom non-commercial Community License (see [LICENSE-COMMUNITY.md](LICENSE-COMMUNITY.md))
- **Creative Assets**: CC BY-NC-SA 4.0 (see [creative/LICENSE.md](creative/LICENSE.md))
- **Commercial Use**: Separate commercial license available (see [LICENSE-COMMERCIAL.md](LICENSE-COMMERCIAL.md))

### Community License Summary

- ✅ Personal, educational, and non-commercial use
- ✅ Creating and sharing demoscene productions
- ✅ Contributing back to the project
- ❌ Commercial products or services
- ❌ Redistribution as standalone SDK

For commercial licensing, please contact the maintainers.

## Philosophy

ShaderLab embraces demoscene principles:

- **Minimalism**: Small footprint, no bloat
- **Performance**: Direct3D 12, optimized compilation
- **Live Coding**: Instant shader feedback
- **Music-Driven**: Beat synchronization at the core
- **Community**: Open development, shared learning

We avoid over-engineering. The codebase prioritizes clarity, performance, and extensibility over feature bloat.

## Contributing

Contributions are welcome! Please:

1. Open an issue to discuss major changes
2. Follow the existing code style
3. Test your changes in Debug and Release
4. Submit a pull request with clear description

All contributions must be licensed under the Community License.

## Project Notes

- Tiny demo build presets target `MicroPlayer` on x86 for size-sensitive outputs.
- Open/free presets target the full runtime on x64.
- Runtime and compact-track debug logging are opt-in build flags and default OFF.
- Clean solution exports now write HLSL source files under `assets/shaders/hlsl/` and store shader links in `project.json` (via `codePath` and `@file:`), instead of embedding full shader source in JSON.

## Credits

Created by the ShaderLab community.

Inspired by:
- ShaderToy (Inigo Quilez and contributors)
- Demoscene tracker tools (Renoise, Buzz)
- Notch Builder, TouchDesigner

## Contact

- **Issues**: [GitHub Issues](https://github.com/drcircuit/ShaderLabDX12/issues)
- **Discussions**: [GitHub Discussions](https://github.com/drcircuit/ShaderLabDX12/discussions)
- **Commercial Licensing**: Open an issue with tag [commercial-license]

---

**ShaderLab** - Realtime shader authoring for the demoscene  
Copyright (c) 2026 ShaderLab Contributors
