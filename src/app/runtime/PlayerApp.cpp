#include "ShaderLab/App/PlayerApp.h"
#include "ShaderLab/Runtime/RuntimeStartupPolicy.h"
#include "ShaderLab/Runtime/RuntimeWindowPolicy.h"

#include "ShaderLab/App/DemoPlayer.h"
#include "ShaderLab/Graphics/CommandQueue.h"
#include "ShaderLab/Graphics/Device.h"
#include "ShaderLab/Graphics/Swapchain.h"

#include <algorithm>
#include <conio.h>
#include <memory>
#include <string>
#ifndef SHADERLAB_TINY_PLAYER
#define SHADERLAB_TINY_PLAYER 0
#endif

#if !SHADERLAB_TINY_PLAYER
#include <filesystem>
#include <iostream>
#endif
#include <windowsx.h>

#ifndef SHADERLAB_RUNTIME_IMGUI
#define SHADERLAB_RUNTIME_IMGUI 1
#endif

#if SHADERLAB_RUNTIME_IMGUI
#include "imgui.h"
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
#endif

namespace ShaderLab {

namespace {

std::string WideToUtf8(const std::wstring& value) {
    if (value.empty()) {
        return {};
    }
    const int required = WideCharToMultiByte(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (required <= 0) {
        return {};
    }
    std::string utf8(static_cast<size_t>(required), '\0');
    const int written = WideCharToMultiByte(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), utf8.data(), required, nullptr, nullptr);
    if (written <= 0) {
        return {};
    }
    return utf8;
}

struct RuntimeResources {
    Device* device = nullptr;
    Swapchain* swapchain = nullptr;
    CommandQueue* commandQueue = nullptr;
    DemoPlayer* player = nullptr;
};

RuntimeResources g_Resources;

struct RuntimeAppState {
    bool debugConsole = false;
    bool windowed = false;
    bool screenSaverMode = false;
    bool vsyncEnabled = true;
    bool startFullscreen = true;
    float lastMeasuredFps = 0.0f;
    double fpsAccumSeconds = 0.0;
    int fpsAccumFrames = 0;
    std::string baseWindowTitle = "DrCiRCUiT's ShaderLab - For democoders, by a democoder - demo";
    RECT windowedRect = { 0, 0, 1280, 720 };
    POINT lastMousePos = { 0, 0 };
    bool runtimeCursorHidden = false;
    bool renderResolutionApplied = false;
};

RuntimeAppState g_Runtime;

void ShutdownRuntimeResources() {
    if (g_Resources.commandQueue) {
        g_Resources.commandQueue->WaitForGPU();
    }
    if (g_Resources.player) {
        g_Resources.player->Shutdown();
        delete g_Resources.player;
        g_Resources.player = nullptr;
    }
    if (g_Resources.swapchain) {
        delete g_Resources.swapchain;
        g_Resources.swapchain = nullptr;
    }
    if (g_Resources.commandQueue) {
        delete g_Resources.commandQueue;
        g_Resources.commandQueue = nullptr;
    }
    if (g_Resources.device) {
        delete g_Resources.device;
        g_Resources.device = nullptr;
    }
}

void ToggleWindowVisibility(HWND hwnd) {
    if (IsWindowVisible(hwnd)) {
        ShowWindow(hwnd, SW_HIDE);
    } else {
        ShowWindow(hwnd, SW_SHOW);
        SetForegroundWindow(hwnd);
    }
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
#if SHADERLAB_RUNTIME_IMGUI
    if (ImGui_ImplWin32_WndProcHandler(hwnd, uMsg, wParam, lParam)) {
        return true;
    }
#endif

    if (uMsg == WM_ERASEBKGND) {
        RECT rc{};
        GetClientRect(hwnd, &rc);
        HDC hdc = reinterpret_cast<HDC>(wParam);
        if (hdc) {
            FillRect(hdc, &rc, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
        }
        return 1;
    }

    if (uMsg == WM_PAINT) {
        PAINTSTRUCT ps{};
        HDC hdc = BeginPaint(hwnd, &ps);
        if (hdc) {
            FillRect(hdc, &ps.rcPaint, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
        }
        EndPaint(hwnd, &ps);
        return 0;
    }

    if (uMsg == WM_SETCURSOR) {
        SetCursor(nullptr);
        return TRUE;
    }

    if (g_Runtime.screenSaverMode) {
        switch (uMsg) {
            case WM_MOUSEMOVE: {
                POINT p;
                p.x = GET_X_LPARAM(lParam);
                p.y = GET_Y_LPARAM(lParam);
                const int dx = p.x - g_Runtime.lastMousePos.x;
                const int dy = p.y - g_Runtime.lastMousePos.y;
                if (dx > 2 || dx < -2 || dy > 2 || dy < -2) {
                    PostQuitMessage(0);
                }
                g_Runtime.lastMousePos = p;
                break;
            }
            case WM_LBUTTONDOWN:
            case WM_RBUTTONDOWN:
            case WM_MBUTTONDOWN:
            case WM_KEYDOWN:
            case WM_SYSKEYDOWN:
                PostQuitMessage(0);
                return 0;
            default:
                break;
        }
    }

    switch (uMsg) {
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        case WM_SYSKEYDOWN:
            if (!g_Runtime.screenSaverMode && wParam == VK_RETURN && (lParam & (1 << 29))) {
                if (g_Runtime.windowed) {
                    RuntimeWindowPolicy::SetFullscreen(hwnd, g_Runtime.windowed, g_Runtime.windowedRect, g_Runtime.baseWindowTitle, g_Runtime.lastMeasuredFps, g_Runtime.vsyncEnabled, g_Runtime.screenSaverMode);
                } else {
                    RuntimeWindowPolicy::SetWindowed(hwnd, g_Runtime.windowed, g_Runtime.windowedRect, g_Runtime.baseWindowTitle, g_Runtime.lastMeasuredFps, g_Runtime.vsyncEnabled, g_Runtime.screenSaverMode);
                }
                return 0;
            }
            break;
        case WM_SIZE:
            if (g_Resources.swapchain) {
                int w = LOWORD(lParam);
                int h = HIWORD(lParam);
                if (w > 0 && h > 0) {
                    g_Resources.swapchain->Resize(static_cast<uint32_t>(w), static_cast<uint32_t>(h));
                    if (g_Resources.player) {
                        g_Resources.player->OnResize(w, h);
                    }
                }
            }
            return 0;
    }
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

} // namespace

int RunPlayerApp(HINSTANCE hInstance, const PlayerLaunchOptions& options) {
    g_Runtime = RuntimeAppState{};
    g_Runtime.debugConsole = options.debugConsole;
    g_Runtime.screenSaverMode = options.screenSaverMode;
    g_Runtime.vsyncEnabled = options.vsyncEnabled;
    g_Runtime.startFullscreen = options.startFullscreen;
    g_Runtime.renderResolutionApplied = false;
    bool startupCompleted = false;

    struct StartupFailureGuard {
        bool* completed = nullptr;

        ~StartupFailureGuard() {
            if (!completed || *completed) {
                return;
            }

            ShutdownRuntimeResources();

            RuntimeStartupPolicy::RestoreRuntimeCursor(g_Runtime.runtimeCursorHidden);
        }
    } startupFailureGuard{&startupCompleted};

    bool singleInstanceAlreadyExists = false;
    RuntimeStartupPolicy::UniqueHandle singleInstanceMutex = RuntimeStartupPolicy::CreateSingleInstanceMutex(singleInstanceAlreadyExists);
    if (!singleInstanceMutex) {
#if !SHADERLAB_TINY_PLAYER
        RuntimeStartupPolicy::EmitRuntimeError("E300", "startup mutex create failed");
#endif
        return -1;
    }
    if (singleInstanceAlreadyExists) {
        RuntimeStartupPolicy::AppendPlayerErrorLogLine("Startup skipped: single-instance mutex already exists.");
        return 0;
    }

    if (g_Runtime.debugConsole) {
        SetEnvironmentVariableA("SHADERLAB_DEBUG_CONSOLE", "1");
        RuntimeStartupPolicy::EnableConsoleLogging();
    }

    const char CLASS_NAME[] = "ShaderLabPlayerWindow";
    WNDCLASS wc = {};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;
    wc.hCursor = nullptr;
    wc.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    RegisterClass(&wc);

    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);
    const bool launchFullscreen = (!g_Runtime.screenSaverMode && g_Runtime.startFullscreen);

    DWORD windowStyle = (g_Runtime.screenSaverMode || launchFullscreen) ? WS_POPUP : WS_OVERLAPPEDWINDOW;
    int windowW = (g_Runtime.screenSaverMode || launchFullscreen) ? screenW : 1280;
    int windowH = (g_Runtime.screenSaverMode || launchFullscreen) ? screenH : 720;
    int windowX = (g_Runtime.screenSaverMode || launchFullscreen) ? 0 : (screenW - windowW) / 2;
    int windowY = (g_Runtime.screenSaverMode || launchFullscreen) ? 0 : (screenH - windowH) / 2;

    const DWORD exStyle = (g_Runtime.screenSaverMode || launchFullscreen) ? WS_EX_TOPMOST : 0;

    HWND hwnd = CreateWindowEx(
        exStyle,
        CLASS_NAME,
        "DrCiRCUiT's ShaderLab - For democoders, by a democoder - demo",
        windowStyle,
        windowX,
        windowY,
        windowW,
        windowH,
        nullptr,
        nullptr,
        hInstance,
        nullptr);

    if (hwnd == nullptr) {
#if !SHADERLAB_TINY_PLAYER
        RuntimeStartupPolicy::EmitRuntimeError("E301", "startup window create failed");
#endif
        return 0;
    }

    const bool deferShowUntilFirstPresent = launchFullscreen;
    if (!deferShowUntilFirstPresent) {
        ShowWindow(hwnd, SW_SHOW);
        UpdateWindow(hwnd);
        SetForegroundWindow(hwnd);
    }

    RuntimeStartupPolicy::HideRuntimeCursor(g_Runtime.runtimeCursorHidden);

    if (g_Runtime.screenSaverMode) {
        GetCursorPos(&g_Runtime.lastMousePos);
        ScreenToClient(hwnd, &g_Runtime.lastMousePos);
    }

    g_Resources.device = new Device();
    if (!g_Resources.device->Initialize()) {
#if !SHADERLAB_TINY_PLAYER
        const char* code = "E102";
        const char* text = "device init failed";
        switch (g_Resources.device->GetLastInitFailureCode()) {
            case Device::InitFailureCode::FactoryCreateFailed:
                code = "E100";
                text = "dxgi factory create failed";
                break;
            case Device::InitFailureCode::AdapterSelectionFailed:
                code = "E101";
                text = "adapter selection failed";
                break;
            case Device::InitFailureCode::DeviceCreateFailed:
                code = "E102";
                text = "d3d12 device create failed";
                break;
            case Device::InitFailureCode::None:
            default:
                code = "E102";
                text = "device init failed";
                break;
        }
        RuntimeStartupPolicy::EmitRuntimeError(code, text);
#endif
        return -1;
    }

#if SHADERLAB_RUNTIME_DEBUG_LOG && !SHADERLAB_TINY_PLAYER
    {
        const auto mem = g_Resources.device->GetVideoMemoryInfo();
        std::cout << "Video device: index=" << g_Resources.device->GetAdapterIndex()
                  << " name=\"" << WideToUtf8(g_Resources.device->GetAdapterName()) << "\""
                  << " localMemUsage=" << mem.usage
                  << " budget=" << mem.budget
                  << std::endl;
    }
#endif

    g_Resources.commandQueue = new CommandQueue();
    if (!g_Resources.commandQueue->Initialize(g_Resources.device, D3D12_COMMAND_LIST_TYPE_DIRECT)) {
#if !SHADERLAB_TINY_PLAYER
        RuntimeStartupPolicy::EmitRuntimeError("E103", "command queue init failed");
#endif
        return -1;
    }

    g_Resources.swapchain = new Swapchain();
    if (!g_Resources.swapchain->Initialize(g_Resources.device, g_Resources.commandQueue, hwnd, static_cast<uint32_t>(windowW), static_cast<uint32_t>(windowH))) {
#if !SHADERLAB_TINY_PLAYER
        RuntimeStartupPolicy::EmitRuntimeError("E104", "swapchain init failed");
#endif
        return -1;
    }

    if (!g_Runtime.screenSaverMode) {
        if (g_Runtime.startFullscreen) {
            RuntimeWindowPolicy::SetFullscreen(hwnd, g_Runtime.windowed, g_Runtime.windowedRect, g_Runtime.baseWindowTitle, g_Runtime.lastMeasuredFps, g_Runtime.vsyncEnabled, g_Runtime.screenSaverMode);
        } else {
            RuntimeWindowPolicy::SetWindowed(hwnd, g_Runtime.windowed, g_Runtime.windowedRect, g_Runtime.baseWindowTitle, g_Runtime.lastMeasuredFps, g_Runtime.vsyncEnabled, g_Runtime.screenSaverMode);
        }
    }

    if (deferShowUntilFirstPresent) {
        if (!RuntimeWindowPolicy::PresentInitialBlackFrame(g_Resources.commandQueue, g_Resources.swapchain)) {
#if !SHADERLAB_TINY_PLAYER
            RuntimeStartupPolicy::EmitRuntimeError("E105", "initial black frame present failed");
#endif
        }
        ShowWindow(hwnd, SW_SHOW);
        UpdateWindow(hwnd);
        SetForegroundWindow(hwnd);
    }

    const int runtimeWidth = static_cast<int>(g_Resources.swapchain->GetWidth());
    const int runtimeHeight = static_cast<int>(g_Resources.swapchain->GetHeight());
    g_Resources.player = new DemoPlayer();
    if (!g_Resources.player->Initialize(hwnd, g_Resources.device, g_Resources.swapchain, runtimeWidth, runtimeHeight)) {
#if !SHADERLAB_TINY_PLAYER
        RuntimeStartupPolicy::EmitRuntimeError("E106", "demo player init failed");
#endif
        return -1;
    }
    g_Resources.player->SetLooping(options.loopPlayback);
    g_Resources.player->SetVsyncEnabled(g_Runtime.vsyncEnabled);

#if SHADERLAB_TINY_PLAYER
    const std::string projectPath = "assets/track.bin";
    const std::string projectName = "demo";
#else
    char exePath[MAX_PATH];
    GetModuleFileNameA(NULL, exePath, MAX_PATH);
    std::filesystem::path exeP(exePath);
    std::filesystem::path jsonPath = exeP.replace_extension(".json");

    if (!std::filesystem::exists(jsonPath)) {
        jsonPath = exeP.parent_path() / "project.json";
    }

    std::string projectPath = jsonPath.string();
    std::string projectName = jsonPath.stem().string();
    if (projectName.empty()) {
        projectName = "demo";
    }
#endif

    if (!g_Runtime.screenSaverMode) {
        g_Runtime.baseWindowTitle = "DrCiRCUiT's ShaderLab - For democoders, by a democoder - " + projectName;
        RuntimeWindowPolicy::UpdateWindowTitleWithStats(hwnd, g_Runtime.baseWindowTitle, g_Runtime.lastMeasuredFps, g_Runtime.vsyncEnabled, g_Runtime.windowed, g_Runtime.screenSaverMode);
    }

    #if !SHADERLAB_TINY_PLAYER
    std::cout << "Loading project: " << projectPath << std::endl;
    #endif
    g_Resources.player->LoadProject(projectPath);

    MSG msg = {};
    LARGE_INTEGER freq, lastTime, currTime;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&lastTime);

    while (msg.message != WM_QUIT) {
        if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);

            if (msg.message == WM_KEYDOWN && msg.wParam == VK_ESCAPE) {
                PostQuitMessage(0);
            }

            if (msg.message == WM_KEYDOWN && g_Runtime.debugConsole && !g_Runtime.screenSaverMode) {
                bool alt = (GetAsyncKeyState(VK_MENU) & 0x8000) != 0;
                if (alt && msg.wParam == 'H') {
                    ToggleWindowVisibility(hwnd);
                } else if (alt && msg.wParam == 'W') {
                    if (!g_Runtime.windowed) {
                        RuntimeWindowPolicy::SetWindowed(hwnd, g_Runtime.windowed, g_Runtime.windowedRect, g_Runtime.baseWindowTitle, g_Runtime.lastMeasuredFps, g_Runtime.vsyncEnabled, g_Runtime.screenSaverMode);
                    } else {
                        RuntimeWindowPolicy::SetFullscreen(hwnd, g_Runtime.windowed, g_Runtime.windowedRect, g_Runtime.baseWindowTitle, g_Runtime.lastMeasuredFps, g_Runtime.vsyncEnabled, g_Runtime.screenSaverMode);
                    }
                }
            }
        } else {
            QueryPerformanceCounter(&currTime);
            double dt = static_cast<double>(currTime.QuadPart - lastTime.QuadPart) / static_cast<double>(freq.QuadPart);
            lastTime = currTime;

            g_Resources.player->Update(0.0, static_cast<float>(dt));

            if (!g_Runtime.renderResolutionApplied && (launchFullscreen || g_Runtime.screenSaverMode)) {
                uint32_t desiredWidth = 0;
                uint32_t desiredHeight = 0;
                if (g_Resources.player->ConsumePendingRenderResolution(desiredWidth, desiredHeight) &&
                    desiredWidth > 0 && desiredHeight > 0) {
                    g_Resources.swapchain->Resize(desiredWidth, desiredHeight);
                    g_Resources.player->OnResize(static_cast<int>(desiredWidth), static_cast<int>(desiredHeight));
                    g_Runtime.renderResolutionApplied = true;
                }
            }

            g_Resources.commandQueue->ResetCommandList();
            auto cmdList = g_Resources.commandQueue->GetCommandList();

            auto* backBuffer = g_Resources.swapchain->GetCurrentBackBuffer();
            D3D12_RESOURCE_BARRIER barrier = {};
            barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier.Transition.pResource = backBuffer;
            barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
            barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
            cmdList->ResourceBarrier(1, &barrier);

            auto rtv = g_Resources.swapchain->GetCurrentRTV();
            float clearColor[] = {0.0f, 0.0f, 0.0f, 1.0f};
            cmdList->ClearRenderTargetView(rtv, clearColor, 0, nullptr);
            cmdList->OMSetRenderTargets(1, &rtv, FALSE, nullptr);

            g_Resources.player->Render(cmdList, backBuffer, rtv);

            g_Runtime.vsyncEnabled = g_Resources.player->IsVsyncEnabled();

            std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
            cmdList->ResourceBarrier(1, &barrier);

            g_Resources.commandQueue->ExecuteCommandList();
            g_Resources.swapchain->Present(g_Runtime.vsyncEnabled);
            g_Resources.commandQueue->WaitForGPU();

            g_Runtime.fpsAccumSeconds += dt;
            g_Runtime.fpsAccumFrames += 1;
            if (g_Runtime.fpsAccumSeconds >= 0.25) {
                g_Runtime.lastMeasuredFps = static_cast<float>(g_Runtime.fpsAccumFrames / g_Runtime.fpsAccumSeconds);
                g_Runtime.fpsAccumSeconds = 0.0;
                g_Runtime.fpsAccumFrames = 0;
                RuntimeWindowPolicy::UpdateWindowTitleWithStats(hwnd, g_Runtime.baseWindowTitle, g_Runtime.lastMeasuredFps, g_Runtime.vsyncEnabled, g_Runtime.windowed, g_Runtime.screenSaverMode);
            }
        }
    }

    ShutdownRuntimeResources();

    RuntimeStartupPolicy::RestoreRuntimeCursor(g_Runtime.runtimeCursorHidden);

    if (g_Runtime.debugConsole) {
#if !SHADERLAB_TINY_PLAYER
        std::cout << "Press any key to exit..." << std::endl;
        _getch();
#endif
    }
    startupCompleted = true;
    return 0;
}

} // namespace ShaderLab
