#include "ShaderLab/App/PlayerApp.h"
#include "ShaderLab/App/DemoPlayer.h"
#include "ShaderLab/Graphics/CommandQueue.h"
#include "ShaderLab/Graphics/Device.h"
#include "ShaderLab/Graphics/Swapchain.h"

namespace ShaderLab {

namespace {

struct TinyRuntimeResources {
    Device* device = nullptr;
    Swapchain* swapchain = nullptr;
    CommandQueue* commandQueue = nullptr;
    DemoPlayer* player = nullptr;
};

TinyRuntimeResources g_resources;

struct TinyRuntimeState {
    bool vsyncEnabled = true;
    bool renderResolutionApplied = false;
};

TinyRuntimeState g_runtime;

void ShutdownTinyRuntimeResources() {
    if (g_resources.commandQueue) {
        g_resources.commandQueue->WaitForGPU();
    }
    if (g_resources.player) {
        g_resources.player->Shutdown();
        delete g_resources.player;
        g_resources.player = nullptr;
    }
    if (g_resources.swapchain) {
        delete g_resources.swapchain;
        g_resources.swapchain = nullptr;
    }
    if (g_resources.commandQueue) {
        delete g_resources.commandQueue;
        g_resources.commandQueue = nullptr;
    }
    if (g_resources.device) {
        delete g_resources.device;
        g_resources.device = nullptr;
    }
}

LRESULT CALLBACK TinyWindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
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

    switch (uMsg) {
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        case WM_SIZE:
            if (g_resources.swapchain) {
                int w = LOWORD(lParam);
                int h = HIWORD(lParam);
                if (w > 0 && h > 0) {
                    g_resources.swapchain->Resize(static_cast<uint32_t>(w), static_cast<uint32_t>(h));
                    if (g_resources.player) {
                        g_resources.player->OnResize(w, h);
                    }
                }
            }
            return 0;
    }
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

} // namespace

int RunPlayerApp(HINSTANCE hInstance, const PlayerLaunchOptions& options) {
    g_runtime = TinyRuntimeState{};
    g_runtime.vsyncEnabled = options.vsyncEnabled;
    g_runtime.renderResolutionApplied = false;

    const wchar_t kClassName[] = L"ShaderLabTinyPlayerWindow";
    WNDCLASSW wc = {};
    wc.lpfnWndProc = TinyWindowProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = kClassName;
    wc.hCursor = nullptr;
    wc.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    RegisterClassW(&wc);

    const int screenW = GetSystemMetrics(SM_CXSCREEN);
    const int screenH = GetSystemMetrics(SM_CYSCREEN);
    const bool launchFullscreen = options.startFullscreen;

    const DWORD windowStyle = launchFullscreen ? WS_POPUP : WS_OVERLAPPEDWINDOW;
    const int windowW = launchFullscreen ? screenW : 1280;
    const int windowH = launchFullscreen ? screenH : 720;
    const int windowX = launchFullscreen ? 0 : (screenW - windowW) / 2;
    const int windowY = launchFullscreen ? 0 : (screenH - windowH) / 2;
    const DWORD exStyle = launchFullscreen ? WS_EX_TOPMOST : 0;

    HWND hwnd = CreateWindowExW(
        exStyle,
        kClassName,
        L"ShaderLab Tiny Player",
        windowStyle,
        windowX,
        windowY,
        windowW,
        windowH,
        nullptr,
        nullptr,
        hInstance,
        nullptr);

    if (!hwnd) {
        return -1;
    }

    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);
    SetForegroundWindow(hwnd);

    g_resources.device = new Device();
    if (!g_resources.device->Initialize()) {
        ShutdownTinyRuntimeResources();
        return -1;
    }

    g_resources.commandQueue = new CommandQueue();
    if (!g_resources.commandQueue->Initialize(g_resources.device, D3D12_COMMAND_LIST_TYPE_DIRECT)) {
        ShutdownTinyRuntimeResources();
        return -1;
    }

    g_resources.swapchain = new Swapchain();
    if (!g_resources.swapchain->Initialize(g_resources.device, g_resources.commandQueue, hwnd, static_cast<uint32_t>(windowW), static_cast<uint32_t>(windowH))) {
        ShutdownTinyRuntimeResources();
        return -1;
    }

    const int runtimeWidth = static_cast<int>(g_resources.swapchain->GetWidth());
    const int runtimeHeight = static_cast<int>(g_resources.swapchain->GetHeight());
    g_resources.player = new DemoPlayer();
    if (!g_resources.player->Initialize(hwnd, g_resources.device, g_resources.swapchain, runtimeWidth, runtimeHeight)) {
        ShutdownTinyRuntimeResources();
        return -1;
    }

    g_resources.player->SetLooping(options.loopPlayback);
    g_resources.player->SetVsyncEnabled(g_runtime.vsyncEnabled);
    g_resources.player->LoadProject("assets/track.bin");

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
        } else {
            QueryPerformanceCounter(&currTime);
            const double dt = static_cast<double>(currTime.QuadPart - lastTime.QuadPart) / static_cast<double>(freq.QuadPart);
            lastTime = currTime;

            g_resources.player->Update(0.0, static_cast<float>(dt));

            if (!g_runtime.renderResolutionApplied && launchFullscreen) {
                uint32_t desiredWidth = 0;
                uint32_t desiredHeight = 0;
                if (g_resources.player->ConsumePendingRenderResolution(desiredWidth, desiredHeight) &&
                    desiredWidth > 0 && desiredHeight > 0) {
                    g_resources.swapchain->Resize(desiredWidth, desiredHeight);
                    g_resources.player->OnResize(static_cast<int>(desiredWidth), static_cast<int>(desiredHeight));
                    g_runtime.renderResolutionApplied = true;
                }
            }

            g_resources.commandQueue->ResetCommandList();
            auto* cmdList = g_resources.commandQueue->GetCommandList();

            auto* backBuffer = g_resources.swapchain->GetCurrentBackBuffer();
            D3D12_RESOURCE_BARRIER barrier = {};
            barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier.Transition.pResource = backBuffer;
            barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
            barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
            cmdList->ResourceBarrier(1, &barrier);

            auto rtv = g_resources.swapchain->GetCurrentRTV();
            float clearColor[] = {0.0f, 0.0f, 0.0f, 1.0f};
            cmdList->ClearRenderTargetView(rtv, clearColor, 0, nullptr);
            cmdList->OMSetRenderTargets(1, &rtv, FALSE, nullptr);

            g_resources.player->Render(cmdList, backBuffer, rtv);
            g_runtime.vsyncEnabled = g_resources.player->IsVsyncEnabled();

            std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
            cmdList->ResourceBarrier(1, &barrier);

            g_resources.commandQueue->ExecuteCommandList();
            g_resources.swapchain->Present(g_runtime.vsyncEnabled);
            g_resources.commandQueue->WaitForGPU();
        }
    }

    ShutdownTinyRuntimeResources();
    return 0;
}

} // namespace ShaderLab
