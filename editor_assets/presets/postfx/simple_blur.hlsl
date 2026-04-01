float4 main(float2 fragCoord, float2 iResolution, float iTime) {
    float2 uv = fragCoord / iResolution;
    float2 texel = 1.0 / iResolution;
    float4 col = float4(0,0,0,0);
    col += iChannel0.Sample(iSampler0, uv + texel * float2(-1, -1));
    col += iChannel0.Sample(iSampler0, uv + texel * float2(0, -1));
    col += iChannel0.Sample(iSampler0, uv + texel * float2(1, -1));
    col += iChannel0.Sample(iSampler0, uv + texel * float2(-1,  0));
    col += iChannel0.Sample(iSampler0, uv + texel * float2(0,  0));
    col += iChannel0.Sample(iSampler0, uv + texel * float2(1,  0));
    col += iChannel0.Sample(iSampler0, uv + texel * float2(-1,  1));
    col += iChannel0.Sample(iSampler0, uv + texel * float2(0,  1));
    col += iChannel0.Sample(iSampler0, uv + texel * float2(1,  1));
    return col / 9.0;
}
