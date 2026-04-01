float4 main(float2 fragCoord, float2 iResolution, float iTime) {
    float2 uv = fragCoord / iResolution;
    float2 dir = uv - 0.5;
    float2 offset = dir * 0.003;
    float r = iChannel0.Sample(iSampler0, uv + offset).r;
    float g = iChannel0.Sample(iSampler0, uv).g;
    float b = iChannel0.Sample(iSampler0, uv - offset).b;
    return float4(r, g, b, 1.0);
}
