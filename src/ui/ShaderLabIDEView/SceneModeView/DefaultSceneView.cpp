#include "ShaderLab/UI/ShaderLabIDE.h"
#include "ShaderLab/UI/UISystemAssets.h"

namespace ShaderLab {

void ShaderLabIDE::CreateDefaultScene() {
    std::string sceneName = "Hello World";
    std::string shaderCode = GetScenePresetCodeByStem("hello_world");
    if (shaderCode.empty()) {
        const auto& scenePresets = GetScenePresets();
        if (!scenePresets.empty()) {
            sceneName = scenePresets[0].name;
            shaderCode = scenePresets[0].code;
        }
    }

    if (shaderCode.empty()) {
        sceneName = "Empty Scene";
        shaderCode = "";
    }

    m_scenes.emplace_back(sceneName, shaderCode);
    m_shaderState.text = shaderCode;
}

} // namespace ShaderLab
