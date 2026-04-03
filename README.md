# ShaderLab

A Windows-native demoscene DX12 SDK and realtime shader authoring environment. 

## Overview

Welcome to ShaderLab! This project is a pet project of mine and I have previously kept it all to myself, but now I decided to share it with the world!

ShaderLab is a minimalist, high-performance toolkit for creating demoscene productions and visual experiments. Built on DirectX 12 and DXC, it provides a tracker-inspired interface for authoring shader-driven visuals synchronized to music.

Project documentation is maintained under `docs/`.

## Getting Started
You install it using the supplied installer. You will also need to download and install the following tools in order to create and build self-contained demos:

### Demo Build Dependencies

To compile and export your demos as standalone executables from within ShaderLab, your system must have the following build tools installed:

- **Visual Studio 2022 Build Tools** (with MSVC v143 compile tools)
https://visualstudio.microsoft.com/downloads/

- **Windows 10/11 SDK**
https://developer.microsoft.com/en-us/windows/downloads/windows-sdk/

- **DXCompiler** (DirectX Shader Compiler)
https://github.com/microsoft/DirectXShaderCompiler/releases

- **CMake**
https://cmake.org/download/

- **Ninja**
https://github.com/ninja-build/ninja/releases

- **Crinkler** (Required for size-restricted micro demo exports)
https://github.com/runestubbe/Crinkler/releases

## Known Bugs
- Sometimes mouse input is blocked in the code editor - I am working on this, but it is mittigated by using arrow keys to release the event handler. This gets the mouse input back. 

- Sometimes scrolling fast "glitches" this is an issue with the 3rd party component used for syntax highlighting. I am waiting for a fix from that repos maintainer.

## Public Repository
ShaderLab is developed as a tool primarily for me, but I have released this version as open source. The primary development of new features will still be under closed source, but those features will make their way onto ShaderLab public - it is just that you may not want all my bugs :)  

Source Code:
https://www.github.com/drcircuit/ShaderLab

## Building from source
You can build this from source, I have a messy walkthrough in BFS.md, but I do recommend using the installer. The installer is built on Github Actions, and packaged with Inno Setup.

## Credits

Created by the DrCiRCUiT (Espen Sande-Larsen).

Inspired by:
- ShaderToy (Inigo Quilez and contributors)
- Demoscene tracker tools (Renoise, Buzz)
- Notch Builder, TouchDesigner


**ShaderLab** - Realtime shader authoring for the demoscene  
Copyright (c) 2026 DrCiRCUiT
