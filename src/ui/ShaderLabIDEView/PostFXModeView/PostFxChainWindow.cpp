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

constexpr int kMaxComputeHistoryCount = 8;

std::string ToLowerAscii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

bool LooksTemporalCompute(const Scene::ComputeEffect& effect) {
    const std::string name = ToLowerAscii(effect.name);
    return name.find("temporal") != std::string::npos ||
           name.find("trail") != std::string::npos ||
           name.find("accum") != std::string::npos;
}

bool LooksDenoiseCompute(const Scene::ComputeEffect& effect) {
    const std::string name = ToLowerAscii(effect.name);
    return name.find("denoise") != std::string::npos ||
           name.find("bilateral") != std::string::npos ||
           name.find("blur") != std::string::npos;
}

}

void ShaderLabIDE::ShowPostFxChainWindow() {
    if (ImGui::Begin("FX: Chain")) {
        // =================================================================
        // PIXEL SHADER EFFECTS (Traditional PostFX)
        // =================================================================
        if (ImGui::CollapsingHeader("Pixel Shader Effects", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Indent();
            
            if (m_postFxDraftChain.empty()) {
                ImGui::TextDisabled("No pixel shader effects in chain.");
            } else {
                for (int i = 0; i < (int)m_postFxDraftChain.size(); ++i) {
                    auto& fx = m_postFxDraftChain[i];
                    ImGui::PushID(i);
                    bool wasEnabled = fx.enabled;
                    ImGui::Checkbox("##Enabled", &fx.enabled);
                    if (fx.enabled != wasEnabled) {
                        RefreshPresetService();
                    }
                    ImGui::SameLine();
                    bool isSelected = (i == m_postFxSelectedIndex);
                    const float buttonBlockWidth = 70.0f + 80.0f + 90.0f + ImGui::GetStyle().ItemSpacing.x * 3.0f;
                    const float nameWidth = (std::max)(16.0f, ImGui::GetContentRegionAvail().x - buttonBlockWidth);
                    if (ImGui::Selectable(fx.name.c_str(), isSelected, 0, ImVec2(nameWidth, 0.0f))) {
                        m_postFxSelectedIndex = i;
                        m_computeEffectSelectedIndex = -1;  // Deselect compute effects
                        SyncPostFxEditorToSelection();
                    }
                    ImGui::SameLine();
                    if (LabeledActionButton("MoveFxUp", OpenFontIcons::kChevronUp, "Up", "Move effect up", ImVec2(70.0f, 0.0f)) && i > 0) {
                        std::swap(m_postFxDraftChain[i], m_postFxDraftChain[i - 1]);
                        m_postFxSelectedIndex = i - 1;
                        RefreshPresetService();
                        ImGui::PopID();
                        continue;
                    }
                    ImGui::SameLine();
                    if (LabeledActionButton("MoveFxDown", OpenFontIcons::kChevronDown, "Down", "Move effect down", ImVec2(80.0f, 0.0f)) && i + 1 < (int)m_postFxDraftChain.size()) {
                        std::swap(m_postFxDraftChain[i], m_postFxDraftChain[i + 1]);
                        m_postFxSelectedIndex = i + 1;
                        RefreshPresetService();
                        ImGui::PopID();
                        continue;
                    }
                    ImGui::SameLine();
                    if (LabeledActionButton("RemoveFx", OpenFontIcons::kTrash2, "Del", "Remove effect", ImVec2(90.0f, 0.0f))) {
                        m_postFxDraftChain.erase(m_postFxDraftChain.begin() + i);
                        if (m_postFxSelectedIndex >= (int)m_postFxDraftChain.size()) {
                            m_postFxSelectedIndex = (int)m_postFxDraftChain.size() - 1;
                        }
                        SyncPostFxEditorToSelection();
                        RefreshPresetService();
                        ImGui::PopID();
                        continue;
                    }
                    ImGui::PopID();
                }
            }
            
            ImGui::Unindent();
        }
        
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        
        // =================================================================
        // COMPUTE EFFECTS (GPU-Accelerated Effects)
        // =================================================================
        if (ImGui::CollapsingHeader("Compute Effects", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Indent();

            int heavyHistoryEffectCount = 0;
            int maxHistoryCount = 0;
            for (const auto& fx : m_computeEffectDraftChain) {
                if (!fx.enabled) {
                    continue;
                }
                maxHistoryCount = (std::max)(maxHistoryCount, fx.historyCount);
                if (fx.historyCount > 2) {
                    ++heavyHistoryEffectCount;
                }
            }

            if (heavyHistoryEffectCount > 1) {
                ImGui::TextColored(
                    m_uiThemeColors.TrackerAccentBeatFontColor,
                    "Warning: %d active compute effects use historyCount > 2 (max %d). This may reduce playback performance.",
                    heavyHistoryEffectCount,
                    maxHistoryCount);
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Try lowering historyCount or disabling extra compute effects to improve frame time.");
                }
                ImGui::Spacing();
            }
            
            if (m_computeEffectDraftChain.empty()) {
                ImGui::TextDisabled("No compute effects in chain.");
                ImGui::TextDisabled("(Temporal accumulation, denoising, etc.)");
            } else {
                for (int i = 0; i < (int)m_computeEffectDraftChain.size(); ++i) {
                    auto& fx = m_computeEffectDraftChain[i];
                    ImGui::PushID(1000 + i);  // Offset IDs to avoid conflicts
                    
                    bool wasEnabled = fx.enabled;
                    ImGui::Checkbox("##Enabled", &fx.enabled);
                    if (fx.enabled != wasEnabled) {
                        RefreshPresetService();
                    }
                    ImGui::SameLine();
                    
                    bool isSelected = (i == m_computeEffectSelectedIndex);
                    const float historyFieldWidth = 56.0f;
                    const float historySpinnerWidth = ImGui::GetFrameHeight() * 2.0f + ImGui::GetStyle().ItemSpacing.x * 2.0f;
                    const float historyBlockWidth = historyFieldWidth + historySpinnerWidth;
                    const float buttonBlockWidth = historyBlockWidth + 70.0f + 80.0f + 90.0f + ImGui::GetStyle().ItemSpacing.x * 4.0f;
                    const float nameWidth = (std::max)(16.0f, ImGui::GetContentRegionAvail().x - buttonBlockWidth);
                    
                    std::string label = fx.name;
                    
                    if (ImGui::Selectable(label.c_str(), isSelected, 0, ImVec2(nameWidth, 0.0f))) {
                        m_computeEffectSelectedIndex = i;
                        m_postFxSelectedIndex = -1;  // Deselect pixel shader effects
                        SyncComputeEditorToSelection();
                    }

                    ImGui::SameLine();
                    auto applyHistoryCount = [&](int requestedCount) {
                        const int historyCount = (std::max)(0, (std::min)(requestedCount, kMaxComputeHistoryCount));
                        if (historyCount == fx.historyCount) {
                            return;
                        }
                        fx.historyCount = historyCount;
                        fx.historyInitialized = false;
                        fx.historyIndex = 0;
                        fx.historyTextures.clear();
                        RefreshPresetService();
                    };

                    int historyCount = (std::max)(0, (std::min)(fx.historyCount, kMaxComputeHistoryCount));
                    ImGui::SetNextItemWidth(historyFieldWidth);
                    PushNumericFont();
                    if (ImGui::InputInt("##HistoryCount", &historyCount, 0, 0)) {
                        applyHistoryCount(historyCount);
                    }
                    PopNumericFont();
                    ImGui::SameLine(0.0f, 2.0f);
                    if (ImGui::Button("-##HistoryDec", ImVec2(ImGui::GetFrameHeight(), ImGui::GetFrameHeight()))) {
                        applyHistoryCount(fx.historyCount - 1);
                    }
                    ImGui::SameLine(0.0f, 2.0f);
                    if (ImGui::Button("+##HistoryInc", ImVec2(ImGui::GetFrameHeight(), ImGui::GetFrameHeight()))) {
                        applyHistoryCount(fx.historyCount + 1);
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("History frames (0-%d)", kMaxComputeHistoryCount);
                    }
                    
                    ImGui::SameLine();
                    if (LabeledActionButton("MoveComputeUp", OpenFontIcons::kChevronUp, "Up", "Move effect up", ImVec2(70.0f, 0.0f)) && i > 0) {
                        std::swap(m_computeEffectDraftChain[i], m_computeEffectDraftChain[i - 1]);
                        m_computeEffectSelectedIndex = i - 1;
                        RefreshPresetService();
                        ImGui::PopID();
                        continue;
                    }
                    ImGui::SameLine();
                    if (LabeledActionButton("MoveComputeDown", OpenFontIcons::kChevronDown, "Down", "Move effect down", ImVec2(80.0f, 0.0f)) && i + 1 < (int)m_computeEffectDraftChain.size()) {
                        std::swap(m_computeEffectDraftChain[i], m_computeEffectDraftChain[i + 1]);
                        m_computeEffectSelectedIndex = i + 1;
                        RefreshPresetService();
                        ImGui::PopID();
                        continue;
                    }
                    ImGui::SameLine();
                    if (LabeledActionButton("RemoveCompute", OpenFontIcons::kTrash2, "Del", "Remove effect", ImVec2(90.0f, 0.0f))) {
                        m_computeEffectDraftChain.erase(m_computeEffectDraftChain.begin() + i);
                        if (m_computeEffectSelectedIndex >= (int)m_computeEffectDraftChain.size()) {
                            m_computeEffectSelectedIndex = (int)m_computeEffectDraftChain.size() - 1;
                        }
                        RefreshPresetService();
                        ImGui::PopID();
                        continue;
                    }
                    
                    // Show parameters inline (collapsed by default)
                    if (isSelected) {
                        ImGui::Indent();
                        ImGui::PushItemWidth(150.0f);
                        
                        const char* param0Label = "Param 0";
                        const char* param1Label = "Param 1";
                        const char* param2Label = "Param 2";
                        const char* param3Label = "Param 3";
                        
                        // Customize labels based on effect naming convention (file/user driven)
                        if (LooksTemporalCompute(fx)) {
                            param0Label = "Decay";
                            param1Label = "Blend";
                            param2Label = "Boost";
                        } else if (LooksDenoiseCompute(fx)) {
                            param0Label = "Radius";
                            param1Label = "Strength";
                            param2Label = "Threshold";
                        }
                        
                        ImGui::SliderFloat(param0Label, &fx.param0, 0.0f, 1.0f);
                        ImGui::SliderFloat(param1Label, &fx.param1, 0.0f, 1.0f);
                        ImGui::SliderFloat(param2Label, &fx.param2, 0.0f, 2.0f);
                        ImGui::SliderFloat(param3Label, &fx.param3, 0.0f, 1.0f);
                        
                        ImGui::PopItemWidth();
                        ImGui::Unindent();
                    }
                    
                    ImGui::PopID();
                }
            }
            
            ImGui::Unindent();
        }
    }
    ImGui::End();
}

}
