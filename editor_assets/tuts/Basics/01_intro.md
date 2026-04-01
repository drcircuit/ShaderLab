# Shader Basics

Welcome to ShaderLab tutorials.

This intro covers core uniforms and a minimal animated scene.

## Core Uniforms

- `iResolution` gives render size in pixels.
- `iTime` advances continuously (seconds).
- `iBeat`, `iBar`, `fBeat`, `fBarBeat`, and `fBarBeat16` are synced to **120 BPM** in tutorial previews.

## Animated Gradient Example

```hlsl
float4 main(float2 fragCoord, float2 iResolution, float iTime)
{
    float2 uv = fragCoord / iResolution;
    float3 col = 0.5 + 0.5 * cos(iTime + uv.xyx + float3(0.0, 2.0, 4.0));
    return float4(col, 1.0);
}
```

## Tips

- Start with one effect at a time.
- Build from grayscale masks to full color.
- Use `fBarBeat` for musically stable motion.
