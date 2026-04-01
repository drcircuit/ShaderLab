#include "ShaderLab/UI/ShaderLabIDE.h"
#include "ShaderLab/UI/ShaderLabIDECore/ActionWidgets.h"
#include "ShaderLab/UI/OpenFontIcons.h"
#include "ShaderLab/UI/UISystemAssets.h"

#include <imgui.h>

#include <algorithm>
#include <cctype>

namespace ShaderLab {

namespace {
using EditorActionWidgets::LabeledActionButton;

int g_selectedPostFxPresetIndex = 0;
int g_selectedComputePresetIndex = 0;

std::string ToLowerAscii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

void ApplyComputeDefaults(Scene::ComputeEffect& effect) {
    effect.threadGroupX = 8;
    effect.threadGroupY = 8;
    effect.threadGroupZ = 1;
    effect.enabled = true;

    switch (effect.type) {
        case Scene::ComputeEffect::Type::Temporal:
            effect.param0 = 0.92f;
            effect.param1 = 0.35f;
            effect.param2 = 1.5f;
            effect.param3 = 0.0f;
            effect.historyCount = 1;
            break;
        case Scene::ComputeEffect::Type::Denoising:
            effect.param0 = 0.5f;
            effect.param1 = 0.8f;
            effect.param2 = 0.1f;
            effect.param3 = 0.0f;
            effect.historyCount = 0;
            break;
        case Scene::ComputeEffect::Type::PostProcess:
            effect.param0 = 1.0f;
            effect.param1 = 0.5f;
            effect.param2 = 0.0f;
            effect.param3 = 0.0f;
            effect.historyCount = 0;
            break;
        case Scene::ComputeEffect::Type::Custom:
        default:
            effect.param0 = 0.0f;
            effect.param1 = 0.0f;
            effect.param2 = 0.0f;
            effect.param3 = 0.0f;
            effect.historyCount = 0;
            break;
    }
}

}

void ShaderLabIDE::ShowPostFxLibraryWindow() {
    if (ImGui::Begin("FX: Library")) {
        // =====================================================================
        // PIXEL SHADER PRESETS
        // =====================================================================
        if (ImGui::CollapsingHeader("Pixel Shader Presets", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Indent();
            
            const auto& presets = GetPostFxPresets();
            if (g_selectedPostFxPresetIndex < 0) {
                g_selectedPostFxPresetIndex = 0;
            }
            if (g_selectedPostFxPresetIndex >= (int)presets.size()) {
                g_selectedPostFxPresetIndex = (int)presets.size() - 1;
            }

            if (ImGui::BeginListBox("##PostFxPresets", ImVec2(-1, 150))) {
                for (int i = 0; i < (int)presets.size(); ++i) {
                    bool isSelected = (i == g_selectedPostFxPresetIndex);
                    if (ImGui::Selectable(presets[i].name.c_str(), isSelected)) {
                        g_selectedPostFxPresetIndex = i;
                    }
                }
                ImGui::EndListBox();
            }

            if (LabeledActionButton("AddPreset", OpenFontIcons::kPlus, "Add Preset", "Add preset effect", ImVec2(170.0f, 0.0f))) {
                if (g_selectedPostFxPresetIndex >= 0 && g_selectedPostFxPresetIndex < (int)presets.size()) {
                    const auto& preset = presets[g_selectedPostFxPresetIndex];
                    m_postFxDraftChain.emplace_back(preset.name, preset.code);
                    m_postFxSelectedIndex = (int)m_postFxDraftChain.size() - 1;
                    SyncPostFxEditorToSelection();
                    RefreshPresetService();
                }
            }
            ImGui::SameLine();
            if (LabeledActionButton("AddEmptyFx", OpenFontIcons::kFilePlus, "Add Empty", "Add empty effect", ImVec2(170.0f, 0.0f))) {
                m_postFxDraftChain.emplace_back("New FX", "float4 main(float2 fragCoord, float2 iResolution, float iTime) {\n    float2 uv = fragCoord / iResolution;\n    return iChannel0.Sample(iSampler0, uv);\n}\n");
                m_postFxSelectedIndex = (int)m_postFxDraftChain.size() - 1;
                SyncPostFxEditorToSelection();
                RefreshPresetService();
            }
            
            ImGui::Unindent();
        }
        
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        
        // =====================================================================
        // COMPUTE EFFECT PRESETS
        // =====================================================================
        if (ImGui::CollapsingHeader("Compute Effect Presets", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Indent();

            const auto& computePresets = GetComputePresets();
            if (g_selectedComputePresetIndex < 0) {
                g_selectedComputePresetIndex = 0;
            }
            if (g_selectedComputePresetIndex >= (int)computePresets.size()) {
                g_selectedComputePresetIndex = (int)computePresets.size() - 1;
            }

            if (ImGui::BeginListBox("##ComputePresets", ImVec2(-1, 150))) {
                for (int i = 0; i < (int)computePresets.size(); ++i) {
                    bool isSelected = (i == g_selectedComputePresetIndex);
                    if (ImGui::Selectable(computePresets[i].name.c_str(), isSelected)) {
                        g_selectedComputePresetIndex = i;
                    }
                }
                ImGui::EndListBox();
            }

            if (LabeledActionButton("AddComputePreset", OpenFontIcons::kPlus, "Add Preset", "Add compute preset effect", ImVec2(170.0f, 0.0f))) {
                if (g_selectedComputePresetIndex >= 0 && g_selectedComputePresetIndex < (int)computePresets.size()) {
                    const auto& preset = computePresets[g_selectedComputePresetIndex];
                    Scene::ComputeEffect newEffect;
                    newEffect.name = preset.name;
                    newEffect.shaderCode = preset.code;
                    newEffect.type = Scene::ComputeEffect::Type::Custom;
                    ApplyComputeDefaults(newEffect);

                    m_computeEffectDraftChain.push_back(newEffect);
                    m_computeEffectSelectedIndex = (int)m_computeEffectDraftChain.size() - 1;
                    m_postFxSelectedIndex = -1;
                    SyncComputeEditorToSelection();
                    RefreshPresetService();
                }
            }
            ImGui::SameLine();
            if (LabeledActionButton("AddEmptyCompute", OpenFontIcons::kFilePlus, "Add Empty", "Add empty compute effect", ImVec2(170.0f, 0.0f))) {
                Scene::ComputeEffect newEffect;
                newEffect.name = "New Compute";
                newEffect.type = Scene::ComputeEffect::Type::Custom;
                newEffect.param0 = 0.0f;
                newEffect.param1 = 0.0f;
                newEffect.param2 = 0.0f;
                newEffect.param3 = 0.0f;
                newEffect.threadGroupX = 8;
                newEffect.threadGroupY = 8;
                newEffect.threadGroupZ = 1;
                newEffect.historyCount = 0;
                newEffect.enabled = true;

                newEffect.shaderCode = R"(cbuffer Params : register(b0) {
    float param0, param1, param2, param3;
    float time;
    float invWidth, invHeight;
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

    float4 current = inputTexture[coord];
    // Add your compute shader logic here
    outputTexture[coord] = current;
}
)";

                m_computeEffectDraftChain.push_back(newEffect);
                m_computeEffectSelectedIndex = (int)m_computeEffectDraftChain.size() - 1;
                m_postFxSelectedIndex = -1;
                SyncComputeEditorToSelection();
                RefreshPresetService();
            }
            
            ImGui::Unindent();
        }
    }
    ImGui::End();
}

}
