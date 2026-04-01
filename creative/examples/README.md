# ShaderLab Examples

This directory contains example shaders and demo projects to help you get started with ShaderLab.

## Shader Examples

### example_gradient.hlsl
A simple gradient shader with beat-synchronized pulse effect. Good starting point for understanding the time/beat constants.

**Features:**
- Normalized UV coordinates
- Beat phase animation
- Color interpolation

### example_tunnel.hlsl
Classic demoscene tunnel effect using raymarching.

**Features:**
- Raymarching SDF
- Camera movement synchronized to time
- Beat-driven color changes
- Surface normal calculation

## Usage

1. Open the ShaderLab Editor
2. In the Effect View, load one of these example shaders
3. Press "Compile" to build the shader
4. Load audio and set BPM in Demo View
5. Watch the shader react to the music in Scene View

## Creating Your Own Shaders

All shaders have access to these constants via `cbuffer TimeConstants : register(b0)`:

```hlsl
cbuffer TimeConstants : register(b0)
{
    float time;           // Audio playback time in seconds
    float beatPhase;      // 0.0 to 1.0 within current quarter note
    float barProgress;    // 0.0 to 1.0 within current bar
    float bpm;            // Beats per minute
};
```

### Shader Entry Point

Pixel shaders should use this signature:
```hlsl
float4 main(float4 position : SV_Position) : SV_Target
{
    // Your code here
    return float4(color, 1.0);
}
```

## Tips

- Use `beatPhase` for smooth animations that loop every beat
- Use `barProgress` for longer animations that span multiple beats
- Normalize screen coordinates: `uv = position.xy / resolution`
- Don't forget aspect ratio correction: `uv.x *= width / height`

## Contributing

Share your shaders! Submit pull requests with interesting effects to help grow the ShaderLab example library.

All shaders in this directory are licensed under CC BY-NC-SA 4.0.
