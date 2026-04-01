float rand(float2 co) {
    return frac(sin(dot(co, float2(12.9898, 78.233))) * 43758.5453);
}

float4 main(float2 fragCoord, float2 iResolution, float iTime) {
    float2 uv = fragCoord / iResolution;
    float4 col = iChannel0.Sample(iSampler0, uv);
    float n = rand(uv * iResolution + iTime * 50.0);
    col.rgb += (n - 0.5) * 0.08;
    return col;
}
