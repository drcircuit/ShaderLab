#pragma once

#include <string>
#include <windows.h>

namespace ShaderLab {
class CommandQueue;
class Swapchain;
}

namespace ShaderLab::RuntimeWindowPolicy {

void UpdateWindowTitleWithStats(
    HWND hwnd,
    const std::string& baseWindowTitle,
    float lastMeasuredFps,
    bool vsyncEnabled,
    bool windowed,
    bool screenSaverMode);

bool PresentInitialBlackFrame(CommandQueue* commandQueue, Swapchain* swapchain);

void SetFullscreen(
    HWND hwnd,
    bool& windowed,
    RECT& windowedRect,
    const std::string& baseWindowTitle,
    float lastMeasuredFps,
    bool vsyncEnabled,
    bool screenSaverMode);

void SetWindowed(
    HWND hwnd,
    bool& windowed,
    RECT& windowedRect,
    const std::string& baseWindowTitle,
    float lastMeasuredFps,
    bool vsyncEnabled,
    bool screenSaverMode);

} // namespace ShaderLab::RuntimeWindowPolicy
