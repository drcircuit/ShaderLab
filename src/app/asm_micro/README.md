# ASM MicroPlayer — Experimental Ultra-Tiny DX12 Demo Player

> **Branch:** `experiment/asm-microplayer`  
> **Status:** Scaffolding complete, first assembly pass

## Goal

Build the smallest possible self-contained demo player by writing the entire
runtime in x64 NASM assembly, eliminating all C/C++ runtime dependencies.
The player renders a single ShaderLab-compatible HLSL pixel shader (cloud tunnel
effect) using Direct3D 12, with beat-synchronised timing driven by a compact
binary track decoder — all in hand-written assembly.

## Architecture

```
┌─────────────────────────────────────────────────────────┐
│  main.asm                                               │
│  ├─ WinMainCRTStartup (no CRT)                         │
│  ├─ Win32 window (fullscreen popup)                     │
│  ├─ DX12 init (Factory, Device, CmdQueue, SwapChain,  │
│  │             Fence, RTV heap, Root Signature, PSO)    │
│  ├─ Main loop (PeekMessage + render)                    │
│  └─ RenderFrame (barrier, clear, draw 3-vert FS tri,   │
│                   barrier, execute, present, fence wait) │
├─────────────────────────────────────────────────────────┤
│  sync_decoder.asm                                       │
│  └─ Decodes TKR3 compact binary track (14-byte header, │
│     transition map, per-scene module map, 9-byte rows)  │
├─────────────────────────────────────────────────────────┤
│  orchestrator.asm                                       │
│  └─ Beat-driven scheduler: wall-clock → exact beat,    │
│     row scanning for scene changes, computes iBeat,     │
│     iBar, fBeat, fBarBeat, fBarBeat16 into constant buf │
├─────────────────────────────────────────────────────────┤
│  shaders/ (HLSL, precompiled to .cso at build time)     │
│  ├─ vs_fullscreen.hlsl  — procedural FS triangle from  │
│  │                        SV_VertexID (no vertex buffer)│
│  └─ ps_cloud_tunnel.hlsl — cloud tunnel effect wrapper  │
├─────────────────────────────────────────────────────────┤
│  shader_data.c  — links precompiled bytecode as symbols │
│  dx12.inc       — COM vtable offsets for x64 DX12 calls │
│  constants.inc  — shared constants & track format defs  │
└─────────────────────────────────────────────────────────┘
```

## Files

| File | Purpose |
|------|---------|
| `main.asm` | Entry point, Win32 window, DX12 init, render loop |
| `sync_decoder.asm` | TKR3 compact track binary decoder |
| `orchestrator.asm` | Beat-driven timing & scene scheduler |
| `constants.inc` | Shared constants (screen res, track format, Win32, DX12) |
| `dx12.inc` | DX12/DXGI COM vtable offsets & helper macros for x64 |
| `shaders/vs_fullscreen.hlsl` | Procedural fullscreen vertex shader |
| `shaders/ps_cloud_tunnel.hlsl` | Cloud tunnel pixel shader wrapper |
| `shader_data.c` | Embeds precompiled shader bytecode |
| `bin2inc.cmake` | Binary → C array conversion script |
| `asm_micro_dummy.c` | Dummy source for CMake (entry is in ASM) |
| `CMakeLists.txt` | Build integration (NASMW + HLSL → .cso → .inc) |

## Building

```powershell
# From the ShaderLab root:
. .\tools\dev_env.ps1

# Configure with ASM micro player enabled
cmake -B build_asm_micro -G Ninja -DSHADERLAB_BUILD_ASM_MICRO=ON

# Build
cmake --build build_asm_micro --target ShaderLabAsmMicro
```

## Key Design Decisions

1. **No CRT** — `WinMainCRTStartup` is the entry point. No C runtime linked.
   All Win32/DX12 calls go through IAT imports directly.

2. **No vertex buffer** — The vertex shader generates a fullscreen triangle
   from `SV_VertexID`, saving ~100 bytes of VB management code.

3. **COM calls via vtable offsets** — `dx12.inc` defines every DX12 COM method
   offset. The `COM_CALL` macro loads the vtable and calls through it.

4. **Precompiled shaders** — HLSL is compiled to `.cso` at build time, then
   converted to C byte arrays and linked as symbols referenced by the ASM.

5. **Track format compatibility** — The sync decoder reads the same TKR3 v3
   format that the IDE exports, so any project can be played.

## Prerequisites

- **NASMW** in `third_party/nasm/nasmw.exe`
- **DXC or FXC** shader compiler (found automatically from Windows SDK or Vulkan SDK)
- **MSVC x64 toolchain**

## Next Steps

- [ ] Verify NASMW assembles all .asm files cleanly
- [ ] Test DX12 initialisation and get first frame on screen
- [ ] Wire up embedded track.bin loading (or hardcode single-scene for testing)
- [ ] Size-optimise: measure .obj sizes, identify compression opportunities
- [ ] Evaluate Crinkler integration for the ASM objects
- [ ] Add audio synthesis (4klang/Oidos style, or external .wav via waveOut)
