// Example HLSL pixel shader for ShaderLab
// This is a simple gradient shader that responds to time and beat

cbuffer TimeConstants : register(b0)
{
    float time;           // Audio time in seconds
    float beatPhase;      // 0.0 to 1.0 within quarter note
    float barProgress;    // 0.0 to 1.0 within bar
    float bpm;
};

float4 main(float4 position : SV_Position) : SV_Target
{
    // Normalize screen coordinates (assumes 1920x1080, adjust as needed)
    float2 uv = position.xy / float2(1920.0, 1080.0);
    uv = uv * 2.0 - 1.0;  // Map to -1..1
    uv.x *= 1920.0 / 1080.0;  // Aspect ratio correction
    
    // Simple gradient with beat pulse
    float pulse = 1.0 - beatPhase;
    pulse = pulse * pulse;  // Smooth falloff
    
    float3 color1 = float3(0.1, 0.2, 0.5);
    float3 color2 = float3(0.8, 0.3, 0.6);
    
    float gradient = length(uv) * 0.5;
    float3 color = lerp(color1, color2, gradient);
    
    // Add beat pulse
    color += pulse * 0.3;
    
    return float4(color, 1.0);
}
