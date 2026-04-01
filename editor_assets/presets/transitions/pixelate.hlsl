float4 main(float2 fragCoord, float2 iResolution, float iTime) {
    int2 dims = max(int2(iResolution) - int2(1, 1), int2(0, 0));
    int2 pixel = clamp(int2(fragCoord), int2(0, 0), dims);
    float t = saturate(iTime);
    float2 uv = (float2(pixel) + 0.5) / iResolution;
    float p = sin(t * 3.14159);
    float n = 50.0 * (1.0 - p) + 1.0;
    float2 uvP = floor(uv * n) / n;
    float4 colA = iChannel0.Sample(iSampler0, uvP);
    float4 colB = iChannel1.Sample(iSampler1, uvP);
    return lerp(colA, colB, t);
}
