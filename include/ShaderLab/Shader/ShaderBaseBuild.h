#pragma once

#include "ShaderLab/Shader/ShaderBase.h"

#include <algorithm>
#include <string>
#include <vector>

namespace ShaderLab::ShaderBase {

inline int DetectUsedSlotCount(const std::string& shaderSource,
                               const std::vector<TextureBindingDecl>& bindings,
                               int maxSlots = 8) {
    int declaredChannels = -1;
    for (int slot = 0; slot < maxSlots; ++slot) {
        if (shaderSource.find("iChannel" + std::to_string(slot)) != std::string::npos ||
            shaderSource.find("iSampler" + std::to_string(slot)) != std::string::npos) {
            declaredChannels = (std::max)(declaredChannels, slot);
        }
    }

    for (const auto& binding : bindings) {
        if (binding.slot >= 0 && binding.slot < maxSlots) {
            declaredChannels = (std::max)(declaredChannels, binding.slot);
        }
    }

    return declaredChannels + 1;
}

inline std::string BuildPreviewPixelShaderTemplate(const std::string& shaderSource,
                                                   const std::vector<TextureBindingDecl>& bindings,
                                                   int slotCount,
                                                   bool flipFragCoord,
                                                   const std::string& shaderEntryPoint) {
    (void)flipFragCoord;
    std::string wrapped;
    wrapped += BuildConstantsBlock();

    for (int slot = 0; slot < slotCount; ++slot) {
        wrapped += ResolveTextureType(slot, bindings) + " iChannel" + std::to_string(slot) +
                   " : register(t" + std::to_string(slot) + ");\n";
    }

    wrapped += "\n";

    for (int slot = 0; slot < slotCount; ++slot) {
        wrapped += "SamplerState iSampler" + std::to_string(slot) + " : register(s" + std::to_string(slot) + ");\n";
    }

    wrapped += R"(

struct PSInput {
    float4 pos : SV_POSITION;
    float2 fragCoord : TEXCOORD0;
};

)";
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

inline std::string BuildBuildPixelShaderTemplate(const std::string& shaderSource,
                                                 const std::vector<TextureBindingDecl>& bindings,
                                                 bool flipFragCoord = false,
                                                 const std::string& shaderEntryPoint = "main") {
    const int slotCount = DetectUsedSlotCount(shaderSource, bindings);
    return BuildPreviewPixelShaderTemplate(shaderSource, bindings, slotCount, flipFragCoord, shaderEntryPoint);
}

inline std::string BuildPlaceholderFragmentModuleSource() {
    return "float4 main(float2 fragCoord, float2 iResolution, float iTime){ return float4(1.0, 0.0, 1.0, 1.0); }";
}

} // namespace ShaderLab::ShaderBase