// Simple raymarched tunnel shader
// Demoscene classic effect

cbuffer TimeConstants : register(b0)
{
    float time;
    float beatPhase;
    float barProgress;
    float bpm;
};

float tunnel(float3 p)
{
    float2 q = float2(length(p.xz) - 1.0, p.y);
    return length(q) - 0.3;
}

float3 getNormal(float3 p)
{
    float2 e = float2(0.001, 0.0);
    return normalize(float3(
        tunnel(p + e.xyy) - tunnel(p - e.xyy),
        tunnel(p + e.yxy) - tunnel(p - e.yxy),
        tunnel(p + e.yyx) - tunnel(p - e.yyx)
    ));
}

float4 main(float4 position : SV_Position) : SV_Target
{
    float2 uv = (position.xy / float2(1920.0, 1080.0)) * 2.0 - 1.0;
    uv.x *= 1920.0 / 1080.0;
    
    // Camera
    float3 ro = float3(0.0, 0.0, time * 2.0);
    float3 rd = normalize(float3(uv, 1.0));
    
    // Raymarch
    float t = 0.0;
    float3 col = float3(0.0, 0.0, 0.0);
    
    for (int i = 0; i < 64; i++)
    {
        float3 p = ro + rd * t;
        float d = tunnel(p);
        
        if (d < 0.001)
        {
            float3 n = getNormal(p);
            float3 light = normalize(float3(sin(time), 1.0, cos(time)));
            float diff = max(0.0, dot(n, light));
            
            // Beat-synchronized color
            float3 color1 = float3(0.2, 0.4, 1.0);
            float3 color2 = float3(1.0, 0.3, 0.5);
            col = lerp(color1, color2, beatPhase) * diff;
            break;
        }
        
        t += d;
        if (t > 20.0) break;
    }
    
    return float4(col, 1.0);
}
