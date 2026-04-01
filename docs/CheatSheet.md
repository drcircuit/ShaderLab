# ShaderLab Cheat Sheet

This document serves as a quick reference for writing specialized HLSL shaders in ShaderLab.  
ShaderLab uses a ShaderToy-inspired structure, allowing you to focus on the pixel shader logic.

## Shader Editor Shortcuts

- `Alt+Shift+F` — Auto-indent / format current shader text.
- `Ctrl+Enter` or `F7` — Compile current shader.
- `Ctrl+F` — Toggle Search/Replace bar.

---

## The Main Function

Your shader must implement a `main` function with the following signature. This is the entry point defined by the system wrapper.

```hlsl
float4 main(float2 fragCoord, float2 iResolution, float iTime) {
    // Return float4(r, g, b, a);
}
```

- **`fragCoord` (float2)**: The pixel coordinates of the current fragment.
  - Range: `(0.0, 0.0)` to `(Width, Height)`
  - Origin: Bottom-left (Standard for ShaderToy/OpenGL context, unlike standard DX12 Top-Left, because the vertex shader flips it).
- **`iResolution` (float2)**: The dimensions of the viewport in pixels.
- **`iTime` (float)**: Time in seconds since the start of playback.

> Compatibility note: `iResolution` and `iTime` are intentionally part of the `main(...)` signature and also engine-provided constants.
> The wrapper forwards engine timing/resolution into your entrypoint so older shaders remain compatible while the renderer keeps a single shared constants model.

### Common Boilerplate
To get normalized UV coordinates (0 to 1):
```hlsl
float2 uv = fragCoord / iResolution;
```

---

## Inputs & Uniforms

The system automatically binds these variables for you.

For Scene/PostFX shaders, you should still read time/resolution via entrypoint parameters (`main(float2 fragCoord, float2 iResolution, float iTime)`).
The constants exist so the engine wrapper and internal pipeline can feed those values consistently.

| Variable | Type | Description |
| :--- | :--- | :--- |
| `iTime` | `float` | Current time in seconds. |
| `iResolution` | `float2` | Viewport resolution (width, height). |

---

## Textures & Channels

ShaderLab supports up to 8 texture slots (Channels 0-7).  
You can bind these in the **"Textures & Channels"** panel to either:
1. **Other Scenes** (for multi-pass effects).
2. **Image Files** (loaded from disk).

### Accessing Textures
Textures correspond to `iChannelX` and samplers to `iSamplerX` (where X is 0-7).

| Channel | Texture Object | Sampler Object |
| :--- | :--- | :--- |
| Channel 0 | `iChannel0` | `iSampler0` |
| Channel 1 | `iChannel1` | `iSampler1` |
| ... | ... | ... |
| Channel 7 | `iChannel7` | `iSampler7` |

### Texture Types
The type of `iChannelX` depends on the setting in the UI:
- **`Texture2D`**: Standard 2D images or scene buffers.
- **`TextureCube`**: Cubemaps (useful for skyboxes/reflections).
- **`Texture3D`**: Volumetric textures.

### Sampling Example
```hlsl
// Sample Channel 0 at the current UV
float4 color = iChannel0.Sample(iSampler0, uv);

// Sample Channel 1
float4 noise = iChannel1.Sample(iSampler1, uv * 2.0);
```

> **Note**: Standard HLSL function `Texture.Sample(SamplerState, Coordinates)` is used.

---

## Coordinate Systems
- **Pixel Coordinates (`fragCoord`)**: `(0, 0)` is **Bottom-Left**.
- **UV Coordinates**: Usually calculated as `fragCoord / iResolution`. `(0, 0)` is Bottom-Left, `(1, 1)` is Top-Right.

---

## Example: Basic Texture Sampling

```hlsl
float4 main(float2 fragCoord, float2 iResolution, float iTime) {
    float2 uv = fragCoord / iResolution;
    
    // Sample texture bound to Channel 0
    float4 texColor = iChannel0.Sample(iSampler0, uv);
    
    // Mix with time-based color
    float3 col = texColor.rgb * (0.5 + 0.5 * sin(iTime));
    
    return float4(col, 1.0);
}
```
