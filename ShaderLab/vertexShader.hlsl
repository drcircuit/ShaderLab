struct VS_OUTPUT
{
    float4 Position : SV_POSITION;
};

VS_OUTPUT main(float4 position : POSITION)
{
    VS_OUTPUT output;
    output.Position = position;
    return output;
}
