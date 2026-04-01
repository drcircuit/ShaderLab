#include "ShaderLab/UI/ShaderLabIDE.h"

namespace ShaderLab {

void ShaderLabIDE::ShowPostFxModeWindows() {
#if SHADERLAB_DEBUG
    if (m_dbgShowPostEffectsWindows) ShowPostEffectsWindows();
    if (m_dbgShowPaletteGenerator) ShowPaletteGeneratorPanel();
    if (m_dbgShowShaderEditor) ShowShaderEditor();
    if (m_dbgShowDiagnostics) ShowDiagnostics();
    if (m_dbgShowPreviewWindow) ShowPreviewWindow();
#else
    ShowPostEffectsWindows();
    ShowPaletteGeneratorPanel();
    ShowShaderEditor();
    ShowDiagnostics();
    ShowPreviewWindow();
#endif
}

} // namespace ShaderLab
