// ============================================================================
//  vs_fullscreen.hlsl — Procedural fullscreen triangle vertex shader
//  No vertex buffer needed: generates position + fragCoord from SV_VertexID.
//
//  Outputs a triangle that covers the full viewport:
//    ID 0: (-1, -1) → (0, H)     bottom-left
//    ID 1: ( 3, -1) → (2W, H)    far right (oversize, clipped)
//    ID 2: (-1,  3) → (0, -H)    far top   (oversize, clipped)
//
//  After clipping, the triangle covers exactly [-1,1]x[-1,1].
//  fragCoord maps to [0,W]x[0,H] matching ShaderLab convention.
// ============================================================================

cbuffer Constants : register(b0) {
    float iTime;
    float2 iResolution;
    float iBeat;
    float iBar;
    float fBeat;
    float fBarBeat;
    float fBarBeat16;
};

struct PSInput {
    float4 pos : SV_POSITION;
    float2 fragCoord : TEXCOORD0;
};

PSInput main(uint vertexID : SV_VertexID) {
    PSInput output;

    // Generate fullscreen triangle from vertex ID
    float2 uv = float2((vertexID << 1) & 2, vertexID & 2);
    output.pos = float4(uv * 2.0 - 1.0, 0.0, 1.0);

    // Flip Y for D3D coordinate system (top-left origin for textures)
    output.pos.y = -output.pos.y;

    // Map UV [0,1] → fragCoord [0, iResolution]
    output.fragCoord = uv * iResolution;

    return output;
}
