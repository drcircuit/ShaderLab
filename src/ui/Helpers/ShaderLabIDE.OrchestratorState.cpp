#include "ShaderLab/UI/ShaderLabIDE.h"

namespace ShaderLab {

void ShaderLabIDE::SetActiveScene(int index) {
    if (index >= (int)m_scenes.size()) return;

    m_activeSceneIndex = index;
    m_editingSceneIndex = index;
    if (index >= 0) {
        auto& scene = m_scenes[index];
        // Editor
        m_shaderState.text = scene.shaderCode;
        m_textEditor.SetText(scene.shaderCode);
        m_shaderState.status = CompileStatus::Clean;
    } else {
        // Clear / Null Scene
        m_shaderState.text = "// No Active Scene";
        m_textEditor.SetText(m_shaderState.text);
        m_shaderState.status = CompileStatus::Clean;
    }
}

void ShaderLabIDE::SyncPostFxEditorToSelection() {
    if (m_postFxSelectedIndex < 0 || m_postFxSelectedIndex >= (int)m_postFxDraftChain.size()) {
        m_shaderState.text = "// No post fx selected";
        m_textEditor.SetText(m_shaderState.text);
        m_shaderState.status = CompileStatus::Clean;
        m_shaderState.diagnostics.clear();
        return;
    }

    auto& effect = m_postFxDraftChain[m_postFxSelectedIndex];
    m_shaderState.text = effect.shaderCode;
    m_textEditor.SetText(effect.shaderCode);
    m_shaderState.status = effect.isDirty ? CompileStatus::Dirty : CompileStatus::Clean;
    m_shaderState.diagnostics.clear();
}

void ShaderLabIDE::SyncComputeEditorToSelection() {
    if (m_computeEffectSelectedIndex < 0 || m_computeEffectSelectedIndex >= (int)m_computeEffectDraftChain.size()) {
        m_shaderState.text = "// No compute effect selected";
        m_textEditor.SetText(m_shaderState.text);
        m_shaderState.status = CompileStatus::Clean;
        m_shaderState.diagnostics.clear();
        return;
    }

    auto& effect = m_computeEffectDraftChain[m_computeEffectSelectedIndex];
    m_shaderState.text = effect.shaderCode;
    m_textEditor.SetText(effect.shaderCode);
    m_shaderState.status = effect.isDirty ? CompileStatus::Dirty : CompileStatus::Clean;
    m_shaderState.diagnostics.clear();
}

void ShaderLabIDE::AppendDemoLog(const std::string& message) {
    m_demoLog.push_back(message);
    if (m_demoLog.size() > 400) {
        m_demoLog.erase(m_demoLog.begin(), m_demoLog.begin() + (m_demoLog.size() - 400));
    }
}

void ShaderLabIDE::UpdateBuildLogic() {
    if (m_isBuilding && m_buildComplete) {
        m_isBuilding = false;
    }
}

} // namespace ShaderLab
