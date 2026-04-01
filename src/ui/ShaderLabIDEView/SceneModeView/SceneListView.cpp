#include "ShaderLab/UI/ShaderLabIDE.h"
#include "ShaderLab/UI/ShaderLabIDECore/ActionWidgets.h"
#include "ShaderLab/UI/OpenFontIcons.h"
#include "ShaderLab/UI/UISystemAssets.h"

#include <imgui.h>

#include <cstdio>

namespace ShaderLab {

namespace {
using EditorActionWidgets::LabeledActionButton;

}

void ShaderLabIDE::ShowSceneList() {
    const char* windowName = (m_currentMode == UIMode::Demo) ? "Demo: Scene Library" : "Scene: Library";
    if (ImGui::Begin(windowName)) {
        ImGui::Text("Scenes");
        ImGui::Separator();

        if (LabeledActionButton("AddScene", OpenFontIcons::kPlus, "Add Scene", "Add scene", ImVec2(276.0f, 0.0f))) {
            char nameBuf[64];
            snprintf(nameBuf, sizeof(nameBuf), "Scene %d", (int)m_scenes.size() + 1);

            const char* templateShader = R"(// ShaderToy-style Hello World
// fragCoord: pixel coordinates (e.g., 0 to 1920x1080)
// iResolution: viewport size (e.g., float2(1920, 1080))
// iTime: time in seconds

float4 main(float2 fragCoord, float2 iResolution, float iTime) {
    // Normalize pixel coordinates (0 to 1)
    float2 uv = fragCoord / iResolution;

    // Create colorful sine wave pattern
    float3 color;
    color.r = 0.5 + 0.5 * sin(uv.x * 10.0 + iTime);
    color.g = 0.5 + 0.5 * sin(uv.y * 10.0 + iTime * 1.3);
    color.b = 0.5 + 0.5 * sin((uv.x + uv.y) * 5.0 + iTime * 0.7);

    return float4(color, 1.0);
}
)";

            m_scenes.emplace_back(nameBuf, templateShader);
            RefreshPresetService();
        }

        ImGui::Separator();

        for (int i = 0; i < (int)m_scenes.size(); i++) {
            ImGui::PushID(i);
            bool isSelected = (i == m_activeSceneIndex);
            if (ImGui::Selectable(m_scenes[i].name.c_str(), isSelected)) {
                SetActiveScene(i);
            }

            if (ImGui::BeginPopupContextItem()) {
                if (ImGui::BeginMenu("Output Type")) {
                     if (ImGui::MenuItem("2D Texture", nullptr, m_scenes[i].outputType == TextureType::Texture2D)) {
                         m_scenes[i].outputType = TextureType::Texture2D;
                         m_scenes[i].texture.Reset();
                     }
                     if (ImGui::MenuItem("Cube Map", nullptr, m_scenes[i].outputType == TextureType::TextureCube)) {
                         m_scenes[i].outputType = TextureType::TextureCube;
                         m_scenes[i].texture.Reset();
                     }
                     ImGui::EndMenu();
                }

                ImGui::Separator();

                if (ImGui::MenuItem("Rename")) {
                }
                if (ImGui::MenuItem("Duplicate")) {
                    m_scenes.push_back(m_scenes[i]);
                    m_scenes.back().name += " (Copy)";
                    RefreshPresetService();
                }
                if (ImGui::MenuItem("Delete", nullptr, false, m_scenes.size() > 1)) {
                    m_scenes.erase(m_scenes.begin() + i);
                    if (m_activeSceneIndex >= (int)m_scenes.size()) {
                        m_activeSceneIndex = (int)m_scenes.size() - 1;
                    }
                    SetActiveScene(m_activeSceneIndex);
                    RefreshPresetService();
                    ImGui::EndPopup();
                    ImGui::PopID();
                    i--;
                    continue;
                }
                ImGui::EndPopup();
            }
            ImGui::PopID();
        }

        if (m_currentMode == UIMode::Scene) {
            ImGui::Separator();
            if (m_activeSceneIndex >= 0 && m_activeSceneIndex < (int)m_scenes.size()) {
                auto& scene = m_scenes[m_activeSceneIndex];
                ImGui::Text("Scene Config");

                char sceneNameBuffer[128];
                std::snprintf(sceneNameBuffer, sizeof(sceneNameBuffer), "%s", scene.name.c_str());
                if (ImGui::InputText("Scene Name", sceneNameBuffer, sizeof(sceneNameBuffer))) {
                    if (sceneNameBuffer[0] == '\0') {
                        std::snprintf(sceneNameBuffer, sizeof(sceneNameBuffer), "Scene %d", m_activeSceneIndex + 1);
                    }
                    scene.name = sceneNameBuffer;
                }

                char sceneDescriptionBuffer[1024];
                std::snprintf(sceneDescriptionBuffer, sizeof(sceneDescriptionBuffer), "%s", scene.description.c_str());
                ImGui::TextUnformatted("Description");
                if (ImGui::InputTextMultiline("##SceneDescription", sceneDescriptionBuffer, sizeof(sceneDescriptionBuffer), ImVec2(-FLT_MIN, ImGui::GetTextLineHeight() * 5.5f))) {
                    scene.description = sceneDescriptionBuffer;
                }
            }
        }
    }
    ImGui::End();
}

}
