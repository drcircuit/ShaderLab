#include "ShaderLab/App/ShaderLabApp.h"
#include <windows.h>
#include <stdio.h>
#include <filesystem>
#include <imgui_impl_win32.h>

namespace {
HICON LoadEditorIcon(int width, int height) {
    namespace fs = std::filesystem;

    fs::path iconPath = fs::path("editor_assets") / "shaderlab.ico.ico";
    if (!fs::exists(iconPath)) {
        std::error_code ec;
        if (fs::exists("editor_assets", ec)) {
            for (const auto& entry : fs::directory_iterator("editor_assets", ec)) {
                if (ec) {
                    break;
                }
                if (!entry.is_regular_file()) {
                    continue;
                }
                const fs::path candidate = entry.path();
                if (candidate.has_extension() && candidate.extension() == ".ico") {
                    iconPath = candidate;
                    break;
                }
            }
        }
    }

    if (!fs::exists(iconPath)) {
        return nullptr;
    }

    return static_cast<HICON>(LoadImageW(
        nullptr,
        iconPath.wstring().c_str(),
        IMAGE_ICON,
        width,
        height,
        LR_LOADFROMFILE));
}
}

using namespace ShaderLab;

// Global app instance
static ShaderLabApp* g_app = nullptr;

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (g_app) {
        return g_app->HandleMessage(hwnd, msg, wParam, lParam);
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    printf("ShaderLab Editor starting...\n");
    (void)nCmdShow;

    ImGui_ImplWin32_EnableDpiAwareness();
    
    // Register window class
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hIcon = LoadEditorIcon(32, 32);
    wc.hIconSm = LoadEditorIcon(16, 16);
    if (!wc.hIcon) {
        wc.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    }
    if (!wc.hIconSm) {
        wc.hIconSm = wc.hIcon;
    }
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = L"ShaderLabIIDEClass";
    if (!RegisterClassExW(&wc)) {
        printf("Failed to register window class\n");
        return -1;
    }
    printf("Window class registered\n");

    // Create borderless window and start maximized (no default titlebar or min/max buttons)
    const int fallbackWidth = 1920;
    const int fallbackHeight = 1080;

    MONITORINFO monitorInfo = {};
    monitorInfo.cbSize = sizeof(MONITORINFO);
    RECT workArea = { 0, 0, fallbackWidth, fallbackHeight };
    HMONITOR monitor = MonitorFromPoint(POINT{ 0, 0 }, MONITOR_DEFAULTTOPRIMARY);
    if (GetMonitorInfo(monitor, &monitorInfo)) {
        workArea = monitorInfo.rcWork;
    }

    int width = workArea.right - workArea.left;
    int height = workArea.bottom - workArea.top;

    HWND hwnd = CreateWindowExW(
        0,
        L"ShaderLabIIDEClass",
        L"DrCiRCUiT's ShaderLab - For democoders, by a democoder - untitled",
        WS_POPUP,
        workArea.left, workArea.top,
        width,
        height,
        nullptr,
        nullptr,
        hInstance,
        nullptr
    );

    if (!hwnd) {
        printf("Failed to create window\n");
        MessageBoxW(nullptr, L"Failed to create window", L"Error", MB_OK | MB_ICONERROR);
        return -1;
    }
    printf("Window created\n");

    SendMessage(hwnd, WM_SETICON, ICON_BIG, (LPARAM)wc.hIcon);
    SendMessage(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)wc.hIconSm);

    // Create and initialize app
    ShaderLabApp app;
    g_app = &app;

    printf("Initializing application...\n");
    if (!app.Initialize(hwnd, width, height)) {
        printf("Failed to initialize application\n");
        MessageBoxW(hwnd, L"Failed to initialize application. Check that Direct3D 12 is available.", L"Error", MB_OK | MB_ICONERROR);
        return -1;
    }
    printf("Application initialized successfully\n");

    ShowWindow(hwnd, SW_SHOWMAXIMIZED);
    SetWindowPos(hwnd, HWND_TOP, workArea.left, workArea.top, width, height, SWP_SHOWWINDOW | SWP_FRAMECHANGED);
    SetForegroundWindow(hwnd);
    SetActiveWindow(hwnd);
    UpdateWindow(hwnd);

    // Main loop
    MSG msg = {};
    LARGE_INTEGER frequency, lastTime, currentTime;
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&lastTime);

    bool running = true;
    while (running) {
        // Process messages
        while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                running = false;
            }
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }

        if (!running) {
            break;
        }

        // Calculate delta time
        QueryPerformanceCounter(&currentTime);
        float deltaTime = static_cast<float>(currentTime.QuadPart - lastTime.QuadPart) / 
                         static_cast<float>(frequency.QuadPart);
        lastTime = currentTime;

        // Update and render
        app.Update(deltaTime);
        app.Render();
    }

    // Cleanup
    app.Shutdown();
    g_app = nullptr;

    return static_cast<int>(msg.wParam);
}
