#include "ShaderLab/UI/ShaderLabIDE.h"

namespace ShaderLab {

void ShaderLabIDE::ShowSceneModeWindows() {
#if SHADERLAB_DEBUG
    if (m_dbgShowSceneList) ShowSceneList();
    if (m_dbgShowSnippetBin) ShowSnippetBin();
    if (m_dbgShowScenePostStack) ShowScenePostStack();
    if (m_dbgShowTexturesAndChannels) ShowSceneTexturesAndChannels();
    if (m_dbgShowPaletteGenerator) ShowPaletteGeneratorPanel();

    if (m_dbgShowShaderEditor) ShowShaderEditor();
    if (m_dbgShowDiagnostics) ShowDiagnostics();
    if (m_dbgShowPreviewWindow) ShowPreviewWindow();
#else
    ShowSceneList();
    ShowSnippetBin();
    ShowScenePostStack();
    ShowSceneTexturesAndChannels();
    ShowPaletteGeneratorPanel();

    ShowShaderEditor();
    ShowDiagnostics();
    ShowPreviewWindow();
#endif
}

} // namespace ShaderLab
