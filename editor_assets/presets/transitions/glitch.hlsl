float4 main(float2 fragCoord, float2 iResolution, float iTime) {
    int2 dims = max(int2(iResolution) - int2(1, 1), int2(0, 0));
    int2 pixel = clamp(int2(fragCoord), int2(0, 0), dims);
    float t = saturate(iTime);
    float2 uv = (float2(pixel) + 0.5) / iResolution;
    float offset = iTime * 10.0;
    float noise = frac(sin(dot(float2(floor(uv.y * 20.0) + offset, offset), float2(12.9898, 78.233))) * 43758.5453);
    float disp = (noise - 0.5) * 0.1 * sin(t * 3.14159);
    float2 uv2 = uv + float2(disp, 0);
    float4 colA = iChannel0.Sample(iSampler0, uv2);
    float4 colB = iChannel1.Sample(iSampler1, uv2);
    return lerp(colA, colB, t);
}
