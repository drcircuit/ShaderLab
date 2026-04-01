float rand(float2 co) {
    return frac(sin(dot(co, float2(12.9898, 78.233))) * 43758.5453);
}

float4 main(float2 fragCoord, float2 iResolution, float iTime) {
    float2 uv = fragCoord / iResolution;
    float band = floor(uv.y * 40.0);
    float shift = (rand(float2(band, iTime)) - 0.5) * 0.02;
    float2 uv2 = uv + float2(shift, 0.0);
    return iChannel0.Sample(iSampler0, uv2);
}
