cbuffer Params : register(b0) {
    float param0;  // Decay
    float param1;  // Blend
    float param2;  // Boost
    float param3;
    float time;
    float invWidth;
    float invHeight;
    uint frame;
};

Texture2D<float4> inputTexture : register(t0);
Texture2D<float4> historyTexture : register(t1);
RWTexture2D<float4> outputTexture : register(u0);

[numthreads(8, 8, 1)]
void main(uint3 threadID : SV_DispatchThreadID) {
    uint2 coord = threadID.xy;
    uint width, height;
    outputTexture.GetDimensions(width, height);
    if (coord.x >= width || coord.y >= height) return;

    float4 current = inputTexture[coord];
    float4 history = historyTexture[coord];
    float4 accumulated = history * param0 + current * param1;
    accumulated.rgb *= param2;
    outputTexture[coord] = saturate(accumulated);
}
