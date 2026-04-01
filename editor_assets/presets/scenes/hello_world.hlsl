// ShaderToy-style Hello World
// fragCoord: pixel coordinates (e.g., 0 to 1920x1080)
// iResolution: viewport size (e.g., float2(1920, 1080))
// iTime: time in seconds

float4 main(float2 fragCoord, float2 iResolution, float iTime) {
    float2 uv = fragCoord / iResolution;

    float3 color;
    color.r = 0.5 + 0.5 * sin(uv.x * 10.0 + iTime);
    color.g = 0.5 + 0.5 * sin(uv.y * 10.0 + iTime * 1.3);
    color.b = 0.5 + 0.5 * sin((uv.x + uv.y) * 5.0 + iTime * 0.7);

    return float4(color, 1.0);
}
