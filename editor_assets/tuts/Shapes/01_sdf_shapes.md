# Shapes with SDF

Signed distance fields are a compact way to draw procedural shapes.

## Circle + Box Blend

```hlsl
float sdCircle(float2 p, float r)
{
    return length(p) - r;
}

float sdBox(float2 p, float2 b)
{
    float2 d = abs(p) - b;
    return length(max(d, 0.0)) + min(max(d.x, d.y), 0.0);
}

float4 main(float2 fragCoord, float2 iResolution, float iTime)
{
    float2 uv = (fragCoord - 0.5 * iResolution.xy) / iResolution.y;

    float t = iTime;
    float2 cPos = float2(0.25 * cos(t), 0.20 * sin(t * 1.3));
    float2 bPos = float2(-0.25 * cos(t * 0.7), -0.15 * sin(t));

    float dCircle = sdCircle(uv - cPos, 0.25);
    float dBox = sdBox(uv - bPos, float2(0.18, 0.18));
    float d = min(dCircle, dBox);

    float edge = smoothstep(0.01, -0.01, d);
    float glow = exp(-8.0 * abs(d));

    float3 base = float3(0.05, 0.08, 0.12);
    float3 ink = float3(0.25, 0.9, 0.8);
    float3 col = lerp(base, ink, edge);
    col += glow * float3(0.12, 0.25, 0.35);

    return float4(col, 1.0);
}
```

## Next Ideas

- Replace `min` with smooth-min for soft blending.
- Add repetition with `frac`.
- Distort UVs using sin waves before distance checks.
