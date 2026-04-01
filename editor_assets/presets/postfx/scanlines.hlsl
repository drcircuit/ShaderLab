float4 main(float2 fragCoord, float2 iResolution, float iTime) {
    float2 uv = fragCoord / iResolution;
    float4 col = iChannel0.Sample(iSampler0, uv);
    float l = sin(uv.y * iResolution.y * 3.14159);
    col.rgb *= 0.9 + 0.1 * l;
    return col;
}
