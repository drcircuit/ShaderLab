#pragma once

#include <string>
#include <vector>

namespace ShaderLab::ShaderBase {

struct TextureBindingDecl {
    int slot;
    std::string type;
};

inline std::string BuildConstantsBlock() {
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
)";
}

inline std::string ResolveTextureType(int slot, const std::vector<TextureBindingDecl>& bindings) {
    for (const auto& binding : bindings) {
        if (binding.slot == slot) {
            return binding.type;
        }
    }
    return "Texture2D";
}

inline std::string BuildTextureDeclarations(const std::vector<TextureBindingDecl>& bindings,
                                            const std::string& texturePrefix) {
    std::string result;
    for (int slot = 0; slot < 8; ++slot) {
        result += ResolveTextureType(slot, bindings) + " " + texturePrefix + std::to_string(slot) +
                  " : register(t" + std::to_string(slot) + ");\n";
    }
    return result;
}

inline std::string BuildSamplerDeclarations(const std::string& samplerPrefix) {
    std::string result;
    for (int slot = 0; slot < 8; ++slot) {
        result += "SamplerState " + samplerPrefix + std::to_string(slot) + " : register(s" + std::to_string(slot) + ");\n";
    }
    return result;
}

inline std::string BuildFragmentShaderTemplate(const std::string& fragmentSource,
                                               const std::vector<TextureBindingDecl>& bindings) {
    std::string wrapped;
    wrapped += BuildConstantsBlock();
    wrapped += BuildTextureDeclarations(bindings, "iChannel");
    wrapped += "\n";
    wrapped += BuildSamplerDeclarations("iChannelSampler");
    wrapped += "\n";
    wrapped += "#line 1 \"user_fragment.hlsl\"\n";
    wrapped += fragmentSource;
    return wrapped;
}

inline std::string BuildPreviewPixelShaderTemplate(const std::string& shaderSource,
                                                   const std::vector<TextureBindingDecl>& bindings,
                                                   bool flipFragCoord,
                                                   const std::string& shaderEntryPoint) {
    (void)flipFragCoord;
    std::string wrapped;
    wrapped += BuildConstantsBlock();
    wrapped += BuildTextureDeclarations(bindings, "iChannel");
    wrapped += "\n";
    wrapped += BuildSamplerDeclarations("iSampler");
    wrapped += R"(

struct PSInput {
    float4 pos : SV_POSITION;
    float2 fragCoord : TEXCOORD0;
};

)";
    wrapped += "#line 1 \"user_preview.hlsl\"\n";
    wrapped += shaderSource;
    wrapped += R"(

float4 PSMain(PSInput input) : SV_TARGET {
)";
    wrapped += "    float2 fragCoord = input.fragCoord;\n";
    wrapped += "    return " + shaderEntryPoint + "(fragCoord, iResolution, iTime);\n";
    wrapped += R"(
}
)";
    return wrapped;
}

} // namespace ShaderLab::ShaderBase