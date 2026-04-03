float4 main(float2 fragCoord, float2 iResolution, float iTime) {
    float2 uv = fragCoord / iResolution;
    float2 texelSize = 1.0 / iResolution;
    
    // FXAA Constants
    float FXAA_SPAN_MAX = 8.0;
    float FXAA_REDUCE_MUL = 1.0 / 8.0;
    float FXAA_REDUCE_MIN = 1.0 / 128.0;

    // Sample neighbors
    float3 rgbNW = iChannel0.Sample(iSampler0, uv + float2(-1.0, -1.0) * texelSize).rgb;
    float3 rgbNE = iChannel0.Sample(iSampler0, uv + float2(1.0, -1.0) * texelSize).rgb;
    float3 rgbSW = iChannel0.Sample(iSampler0, uv + float2(-1.0, 1.0) * texelSize).rgb;
    float3 rgbSE = iChannel0.Sample(iSampler0, uv + float2(1.0, 1.0) * texelSize).rgb;
    float3 rgbM  = iChannel0.Sample(iSampler0, uv).rgb;

    // Calculate Luminance
    float3 lumaCoef = float3(0.299, 0.587, 0.114);
    float lumaNW = dot(rgbNW, lumaCoef);
    float lumaNE = dot(rgbNE, lumaCoef);
    float lumaSW = dot(rgbSW, lumaCoef);
    float lumaSE = dot(rgbSE, lumaCoef);
    float lumaM  = dot(rgbM,  lumaCoef);

    float lumaMin = min(lumaM, min(min(lumaNW, lumaNE), min(lumaSW, lumaSE)));
    float lumaMax = max(lumaM, max(max(lumaNW, lumaNE), max(lumaSW, lumaSE)));

    // Edge direction
    float2 dir;
    dir.x = -((lumaNW + lumaNE) - (lumaSW + lumaSE));
    dir.y =  ((lumaNW + lumaSW) - (lumaNE + lumaSE));

    float dirReduce = max((lumaNW + lumaNE + lumaSW + lumaSE) * (0.25 * FXAA_REDUCE_MUL), FXAA_REDUCE_MIN);
    float rcpDirMin = 1.0 / (min(abs(dir.x), abs(dir.y)) + dirReduce);

    dir = min(float2(FXAA_SPAN_MAX, FXAA_SPAN_MAX), 
          max(float2(-FXAA_SPAN_MAX, -FXAA_SPAN_MAX), dir * rcpDirMin)) * texelSize;

    // First blend sample
    float3 rgbA = (1.0/2.0) * (
        iChannel0.Sample(iSampler0, uv + dir * (1.0/3.0 - 0.5)).rgb +
        iChannel0.Sample(iSampler0, uv + dir * (2.0/3.0 - 0.5)).rgb);

    // Second blend sample
    float3 rgbB = rgbA * (1.0/2.0) + (1.0/4.0) * (
        iChannel0.Sample(iSampler0, uv + dir * (0.0/3.0 - 0.5)).rgb +
        iChannel0.Sample(iSampler0, uv + dir * (3.0/3.0 - 0.5)).rgb);

    float lumaB = dot(rgbB, lumaCoef);

    // If the second blend went out of bounds (too far past the edge), return the safer first blend
    if((lumaB < lumaMin) || (lumaB > lumaMax)) {
        return float4(rgbA, 1.0);
    }
    
    return float4(rgbB, 1.0);
}
