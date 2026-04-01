#include "ShaderLab/Runtime/RuntimeWindowPolicy.h"

#include "ShaderLab/Graphics/CommandQueue.h"
#include "ShaderLab/Graphics/Swapchain.h"

#include <algorithm>
#include <cstdio>

namespace ShaderLab::RuntimeWindowPolicy {

void UpdateWindowTitleWithStats(
    HWND hwnd,
    const std::string& baseWindowTitle,
    float lastMeasuredFps,
    bool vsyncEnabled,
    bool windowed,
    bool screenSaverMode) {
    if (!hwnd || screenSaverMode) {
        return;
    }

    char title[512] = {};
    std::snprintf(
        title,
        sizeof(title),
        "%s | FPS: %.1f | VSync: %s | Fullscreen: %s",
        baseWindowTitle.c_str(),
        lastMeasuredFps,
        vsyncEnabled ? "On" : "Off",
        windowed ? "Windowed" : "Borderless");
    SetWindowTextA(hwnd, title);
}

bool PresentInitialBlackFrame(CommandQueue* commandQueue, Swapchain* swapchain) {
    if (!commandQueue || !swapchain) {
        return false;
    }

    commandQueue->ResetCommandList();
    auto* cmdList = commandQueue->GetCommandList();
    if (!cmdList) {
        return false;
    }

    auto* backBuffer = swapchain->GetCurrentBackBuffer();
    if (!backBuffer) {
        return false;
    }

    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = backBuffer;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    cmdList->ResourceBarrier(1, &barrier);

    auto rtv = swapchain->GetCurrentRTV();
    float clearColor[] = {0.0f, 0.0f, 0.0f, 1.0f};
    cmdList->ClearRenderTargetView(rtv, clearColor, 0, nullptr);
    cmdList->OMSetRenderTargets(1, &rtv, FALSE, nullptr);

    std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
    cmdList->ResourceBarrier(1, &barrier);

    commandQueue->ExecuteCommandList();
    swapchain->Present(true);
    return true;
}

void SetFullscreen(
    HWND hwnd,
    bool& windowed,
    RECT& windowedRect,
    const std::string& baseWindowTitle,
    float lastMeasuredFps,
    bool vsyncEnabled,
    bool screenSaverMode) {
    if (!hwnd) {
        return;
    }

    if (windowed) {
        GetWindowRect(hwnd, &windowedRect);
    }

    windowed = false;
    DWORD style = WS_POPUP | WS_VISIBLE;
    SetWindowLongPtr(hwnd, GWL_STYLE, style);
    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);
    SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, screenW, screenH, SWP_FRAMECHANGED);

    UpdateWindowTitleWithStats(hwnd, baseWindowTitle, lastMeasuredFps, vsyncEnabled, windowed, screenSaverMode);
}

void SetWindowed(
    HWND hwnd,
    bool& windowed,
    RECT& windowedRect,
    const std::string& baseWindowTitle,
    float lastMeasuredFps,
    bool vsyncEnabled,
    bool screenSaverMode) {
    if (!hwnd) {
        return;
    }

    windowed = true;
    DWORD style = WS_OVERLAPPEDWINDOW | WS_VISIBLE;
    SetWindowLongPtr(hwnd, GWL_STYLE, style);

    int width = windowedRect.right - windowedRect.left;
    int height = windowedRect.bottom - windowedRect.top;
    if (width <= 0 || height <= 0) {
        width = 1280;
        height = 720;
    }
    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);
    int x = (screenW - width) / 2;
    int y = (screenH - height) / 2;
    SetWindowPos(hwnd, HWND_NOTOPMOST, x, y, width, height, SWP_FRAMECHANGED | SWP_SHOWWINDOW);

    UpdateWindowTitleWithStats(hwnd, baseWindowTitle, lastMeasuredFps, vsyncEnabled, windowed, screenSaverMode);
}

} // namespace ShaderLab::RuntimeWindowPolicy
