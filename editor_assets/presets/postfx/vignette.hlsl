float4 main(float2 fragCoord, float2 iResolution, float iTime) {
    float2 uv = fragCoord / iResolution;
    float2 p = uv - 0.5;
    float v = 1.0 - smoothstep(0.2, 0.7, dot(p, p));
    float4 col = iChannel0.Sample(iSampler0, uv);
    col.rgb *= v;
    return col;
}
