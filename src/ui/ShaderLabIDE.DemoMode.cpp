#include "ShaderLab/UI/ShaderLabIDE.h"

namespace ShaderLab {

void ShaderLabIDE::ShowDemoModeWindows() {
    ShowDemoMetadata();
    ShowDemoPlaylist();
    ShowAudioLibrary();

    ShowSceneList();
    ShowDemoRuntimeLogWindow();

    ShowPreviewWindow();
}

} // namespace ShaderLab
