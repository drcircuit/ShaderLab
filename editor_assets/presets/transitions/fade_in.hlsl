float4 main(float2 fragCoord, float2 iResolution, float iTime) {
    int2 dims = max(int2(iResolution) - int2(1, 1), int2(0, 0));
    int2 pixel = clamp(int2(fragCoord), int2(0, 0), dims);
    float t = saturate(iTime);
    float4 colB = iChannel1.Load(int3(pixel, 0));
    return lerp(float4(0,0,0,1), colB, t);
}
