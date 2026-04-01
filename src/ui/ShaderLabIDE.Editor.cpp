#include "ShaderLab/UI/ShaderLabIDE.h"

namespace ShaderLab {

void ShaderLabIDE::ShowModeWindows() {
    if (m_previewFullscreen) {
        ShowFullscreenPreview();
        return;
    }

    switch (m_currentMode) {
        case UIMode::Demo:
            ShowDemoModeWindows();
            break;
        case UIMode::Scene:
            ShowSceneModeWindows();
            break;
        case UIMode::PostFX:
            ShowPostFxModeWindows();
            break;
        default:
            break;
    }
}

} // namespace ShaderLab
