#include "ShaderLab/UI/ShaderLabIDE.h"

#include <imgui.h>

namespace ShaderLab {

void ShaderLabIDE::ShowScenePostStack() {
    if (ImGui::Begin("Scene: Post Stack")) {
        ImGui::Text("Post Processing Stack");
        ImGui::Separator();

        if (m_activeSceneIndex >= 0 && m_activeSceneIndex < (int)m_scenes.size()) {
            auto& scene = m_scenes[m_activeSceneIndex];
            ImGui::Text("Active Scene: %s", scene.name.c_str());
            ImGui::Separator();

            if (ImGui::CollapsingHeader("Pixel Shader Effects", ImGuiTreeNodeFlags_DefaultOpen)) {
                if (scene.postFxChain.empty()) {
                    ImGui::TextDisabled("No pixel shader effects assigned.");
                } else {
                    for (size_t i = 0; i < scene.postFxChain.size(); ++i) {
                        const auto& fx = scene.postFxChain[i];
                        ImGui::BulletText("%s%s", fx.enabled ? "" : "(Disabled) ", fx.name.c_str());
                    }
                }
            }

            if (ImGui::CollapsingHeader("Compute Effects", ImGuiTreeNodeFlags_DefaultOpen)) {
                if (scene.computeEffectChain.empty()) {
                    ImGui::TextDisabled("No compute effects assigned.");
                } else {
                    for (size_t i = 0; i < scene.computeEffectChain.size(); ++i) {
                        const auto& fx = scene.computeEffectChain[i];
                        const char* typeLabel = "Custom";
                        switch (fx.type) {
                            case Scene::ComputeEffect::Type::Temporal: typeLabel = "Temporal"; break;
                            case Scene::ComputeEffect::Type::Denoising: typeLabel = "Denoise"; break;
                            case Scene::ComputeEffect::Type::PostProcess: typeLabel = "PostProcess"; break;
                            case Scene::ComputeEffect::Type::Custom: typeLabel = "Custom"; break;
                        }
                        ImGui::BulletText("[%s] %s%s", typeLabel, fx.enabled ? "" : "(Disabled) ", fx.name.c_str());
                    }
                }
            }

        } else {
            ImGui::TextDisabled("No active scene selected.");
        }
    }
    ImGui::End();
}

}
