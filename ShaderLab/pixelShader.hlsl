struct PS_INPUT
{
    float4 Position : SV_POSITION;
};

cbuffer TimeBuffer : register(b0)
{
    float elapsedTime;
};

cbuffer ResolutionBuffer : register(b1)
{
    float2 resolution;
};

float4 main(PS_INPUT input) : SV_TARGET
{
    float2 uv = (input.Position.xy / resolution.xy) * 2.0f - 1.0f;
    float speed= 0.5f;
    float3 col = 0.5f + 0.5f * cos(elapsedTime * speed + uv.xyx + float3(0, 2, 4));
    return float4(col, 1.0f);
}