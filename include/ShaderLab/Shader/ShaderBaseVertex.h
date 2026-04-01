#pragma once

#include <string>

namespace ShaderLab::ShaderBase {

inline std::string BuildFullscreenQuadVertexShaderSource() {
    return R"(
cbuffer Constants : register(b0) {
    float iTime;
    float2 iResolution;
    float iBeat;
    float iBar;
    float fBeat;
    float fBarBeat;
    float fBarBeat16;
};

struct VSInput {
    float3 pos : POSITION;
    float2 uv : TEXCOORD0;
};

struct PSInput {
    float4 pos : SV_POSITION;
    float2 fragCoord : TEXCOORD0;
};

PSInput main(VSInput input) {
    PSInput output;
    output.pos = float4(input.pos, 1.0);
    output.fragCoord = input.uv * iResolution;
    return output;
}
)";
}

} // namespace ShaderLab::ShaderBase