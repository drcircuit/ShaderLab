// ============================================================================
//  ps_cloud_tunnel.hlsl — Cloud tunnel effect wrapped for ASM MicroPlayer
//  This wraps the cloud_tunnel.hlsl effect with the PSMain entry point
//  that the D3D12 PSO expects.
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

// ── Include the cloud tunnel effect code ───────────────────────────────────
// The effect defines: float4 main(float2 fragCoord, float2 iResolution, float iTime)
// We must rename it to avoid collision with our entry point.

#define main cloud_tunnel_main
#include "cloud_tunnel.hlsl"
#undef main

float4 PSMain(PSInput input) : SV_TARGET {
    return cloud_tunnel_main(input.fragCoord, iResolution, iTime);
}
