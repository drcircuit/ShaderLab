cbuffer Params : register(b0) {
    float param0;  // Radius
    float param1;  // Strength
    float param2;  // Threshold
    float param3;
    float time;
    float invWidth;
    float invHeight;
    uint frame;
};

Texture2D<float4> inputTexture : register(t0);
RWTexture2D<float4> outputTexture : register(u0);

[numthreads(8, 8, 1)]
void main(uint3 threadID : SV_DispatchThreadID) {
    uint2 coord = threadID.xy;
    uint width, height;
    outputTexture.GetDimensions(width, height);
    if (coord.x >= width || coord.y >= height) return;

    float4 center = inputTexture[coord];
    outputTexture[coord] = center;
}
