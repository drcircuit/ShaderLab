#include "ShaderLab/UI/ShaderLabIDE.h"
#include "ShaderLab/UI/ShaderLabIDECore/ActionWidgets.h"
#include "ShaderLab/UI/OpenFontIcons.h"
#include "ShaderLab/UI/UISystemAssets.h"

#include <imgui.h>

#include <vector>

namespace ShaderLab {

namespace {
using EditorActionWidgets::LabeledActionButton;

}

void ShaderLabIDE::ShowPostFxSourceWindow() {
    if (ImGui::Begin("FX: Source")) {
        std::vector<const char*> sceneNames;
        sceneNames.reserve(m_scenes.size() + 1);
        sceneNames.push_back("(None)");
        for (const auto& s : m_scenes) {
            sceneNames.push_back(s.name.c_str());
        }

        int sourceIndex = m_postFxSourceSceneIndex >= 0 ? m_postFxSourceSceneIndex + 1 : 0;
        if (ImGui::Combo("Source Scene", &sourceIndex, sceneNames.data(), (int)sceneNames.size())) {
            int newIndex = sourceIndex - 1;
            if (newIndex != m_postFxSourceSceneIndex) {
                m_postFxSourceSceneIndex = newIndex;
                if (m_postFxSourceSceneIndex >= 0 && m_postFxSourceSceneIndex < (int)m_scenes.size()) {
                    m_postFxDraftChain = m_scenes[m_postFxSourceSceneIndex].postFxChain;
                    m_computeEffectDraftChain = m_scenes[m_postFxSourceSceneIndex].computeEffectChain;
                } else {
                    m_postFxDraftChain.clear();
                    m_computeEffectDraftChain.clear();
                }
                m_postFxSelectedIndex = m_postFxDraftChain.empty() ? -1 : 0;
                m_computeEffectSelectedIndex = m_computeEffectDraftChain.empty() ? -1 : 0;
                if (m_postFxSelectedIndex >= 0) {
                    SyncPostFxEditorToSelection();
                } else {
                    SyncComputeEditorToSelection();
                }
            }
        }

        if (m_postFxSourceSceneIndex >= 0 && m_postFxSourceSceneIndex < (int)m_scenes.size()) {
            if (LabeledActionButton("ApplyFxDraft", OpenFontIcons::kCheck, "Apply", "Apply draft chains to scene", ImVec2(110.0f, 0.0f))) {
                m_scenes[m_postFxSourceSceneIndex].postFxChain = m_postFxDraftChain;
                m_scenes[m_postFxSourceSceneIndex].computeEffectChain = m_computeEffectDraftChain;
                RefreshPresetService();
            }
        }
    }
    ImGui::End();
}

}
