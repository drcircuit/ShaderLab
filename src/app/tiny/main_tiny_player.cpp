#include <windows.h>

#include "ShaderLab/App/PlayerApp.h"

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int) {
    ShaderLab::PlayerLaunchOptions options{};
    options.debugConsole = false;
    options.loopPlayback = true;
    options.screenSaverMode = false;
    options.vsyncEnabled = true;
    options.startFullscreen = true;

    return ShaderLab::RunPlayerApp(hInstance, options);
}
