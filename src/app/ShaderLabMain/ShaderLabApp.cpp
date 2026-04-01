#include "ShaderLab/App/ShaderLabApp.h"
#include "ShaderLab/Graphics/Device.h"
#include "ShaderLab/Graphics/CommandQueue.h"
#include "ShaderLab/Graphics/Swapchain.h"
#include "ShaderLab/Graphics/PreviewRenderer.h"
#include "ShaderLab/UI/ShaderLabIDE.h"
#include "ShaderLab/Audio/AudioSystem.h"
#include "ShaderLab/Audio/BeatClock.h"
#include "ShaderLab/Shader/ShaderCompiler.h"

#include <dwmapi.h>
#include <windowsx.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <cstdio>
#include <exception>

#pragma comment(lib, "dwmapi.lib")

// Forward declare Win32 message handler
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace ShaderLab {

namespace {
std::wstring ToWide(const std::string& value) {
    if (value.empty()) {
        return std::wstring();
    }

    UINT codePage = CP_UTF8;
    int size = MultiByteToWideChar(codePage, MB_ERR_INVALID_CHARS, value.data(), (int)value.size(), nullptr, 0);
    if (size == 0) {
        codePage = CP_ACP;
        size = MultiByteToWideChar(codePage, 0, value.data(), (int)value.size(), nullptr, 0);
    }
    if (size == 0) {
        return std::wstring();
    }

    std::wstring wide(size, L'\0');
    MultiByteToWideChar(codePage, 0, value.data(), (int)value.size(), wide.data(), size);
    return wide;
}

std::string GetShaderLabLogPath() {
    char tempPath[MAX_PATH] = {};
    DWORD len = GetTempPathA(MAX_PATH, tempPath);
    if (len > 0 && len < MAX_PATH) {
        return std::string(tempPath) + "shaderlab_log.txt";
    }
    return "shaderlab_log.txt";
}
}

ShaderLabApp::ShaderLabApp() = default;
ShaderLabApp::~ShaderLabApp() = default;

bool ShaderLabApp::Initialize(HWND hwnd, uint32_t width, uint32_t height) {
    m_width = width;
    m_height = height;
    m_hwnd = hwnd;
    
    SetWindowTextW(m_hwnd, L"DrCiRCUiT's ShaderLab - For democoders, by a democoder - untitled");

    if (m_useCustomTitlebar) {
        ConfigureCustomTitlebar();
    }

    // Initialize graphics
    m_device = std::make_unique<Device>();
    if (!m_device->Initialize(true, m_currentAdapterIndex)) {  // Enable validation in debug
        MessageBoxW(hwnd, L"Failed to initialize D3D12 Device", L"Initialization Error", MB_OK | MB_ICONERROR);
        return false;
    }

    m_commandQueue = std::make_unique<CommandQueue>();
    if (!m_commandQueue->Initialize(m_device.get(), D3D12_COMMAND_LIST_TYPE_DIRECT)) {
        MessageBoxW(hwnd, L"Failed to initialize Command Queue", L"Initialization Error", MB_OK | MB_ICONERROR);
        return false;
    }

    m_swapchain = std::make_unique<Swapchain>();
    if (!m_swapchain->Initialize(m_device.get(), m_commandQueue.get(), hwnd, width, height)) {
        MessageBoxW(hwnd, L"Failed to initialize Swapchain", L"Initialization Error", MB_OK | MB_ICONERROR);
        return false;
    }

    // Initialize UI
    m_ui = std::make_unique<ShaderLabIDE>();
    if (!m_ui->Initialize(hwnd, m_device.get(), m_swapchain.get())) {
        MessageBoxW(hwnd, L"Failed to initialize UI System", L"Initialization Error", MB_OK | MB_ICONERROR);
        return false;
    }

    // Initialize audio
    m_audio = std::make_unique<AudioSystem>();
    if (!m_audio->Initialize()) {
        MessageBoxW(hwnd, L"Failed to initialize Audio System", L"Initialization Error", MB_OK | MB_ICONERROR);
        return false;
    }

    m_beatClock = std::make_unique<BeatClock>();
    m_beatClock->SetBPM(140.0f);

    // Initialize shader compiler
    m_shaderCompiler = std::make_unique<ShaderCompiler>();
    const std::string logPath = GetShaderLabLogPath();
    FILE* f = fopen(logPath.c_str(), "a");
    if (f) { fprintf(f, "About to initialize shader compiler\n"); fclose(f); }
    if (!m_shaderCompiler->Initialize()) {
        MessageBoxW(hwnd, L"Failed to initialize Shader Compiler (DXC)", L"Initialization Error", MB_OK | MB_ICONERROR);
        return false;
    }
    f = fopen(logPath.c_str(), "a");
    if (f) { fprintf(f, "Shader compiler initialized\n"); fclose(f); }

    // Initialize preview renderer
    m_previewRenderer = std::make_unique<PreviewRenderer>();
    f = fopen(logPath.c_str(), "a");
    if (f) { fprintf(f, "About to initialize preview renderer\n"); fclose(f); }
    if (!m_previewRenderer->Initialize(m_device.get(), m_shaderCompiler.get(), DXGI_FORMAT_R8G8B8A8_UNORM, nullptr)) {
        MessageBoxW(hwnd, L"Failed to initialize Preview Renderer", L"Initialization Error", MB_OK | MB_ICONERROR);
        return false;
    }
    f = fopen(logPath.c_str(), "a");
    if (f) { fprintf(f, "Preview renderer initialized\n"); fclose(f); }

    // Pass preview renderer to UI system
    m_ui->SetPreviewRenderer(m_previewRenderer.get());
    m_ui->SetAudioSystem(m_audio.get());
    m_ui->SetRestartCallback([this](int index) { RequestRestart(index); });

    f = fopen(logPath.c_str(), "a");
    if (f) { fprintf(f, "All initialization complete\n"); fclose(f); }

    m_initialized = true;
    return true;
}

void ShaderLabApp::ConfigureCustomTitlebar() {
    BOOL enableDark = TRUE;
    DwmSetWindowAttribute(m_hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &enableDark, sizeof(enableDark));

    MARGINS margins = {0, 0, 1, 0};
    DwmExtendFrameIntoClientArea(m_hwnd, &margins);
}

void ShaderLabApp::Shutdown() {
    if (!m_initialized) {
        return;
    }

    m_commandQueue->WaitForGPU();

    m_previewRenderer->Shutdown();
    m_shaderCompiler->Shutdown();
    m_beatClock.reset();
    m_audio->Shutdown();
    m_ui->Shutdown();
    m_swapchain->Shutdown();
    m_commandQueue->Shutdown();
    m_device->Shutdown();

    m_initialized = false;
}

void ShaderLabApp::Update(float deltaTime) {
    if (m_pendingRestart) {
        PerformRestart();
        return;
    }

    if (!m_initialized) {
        return;
    }

    if (!m_appActive) {
        Sleep(16);
        return;
    }

    // Update UI transport
    static double lastWallTime = 0.0;
    LARGE_INTEGER counter, frequency;
    QueryPerformanceCounter(&counter);
    QueryPerformanceFrequency(&frequency);
    double currentWallTime = static_cast<double>(counter.QuadPart) / static_cast<double>(frequency.QuadPart);
    
    m_ui->UpdateTransport(currentWallTime, deltaTime);
    lastWallTime = currentWallTime;

    UpdateWindowTitle();

    // Update beat clock with transport time
    const auto& transport = m_ui->GetTransport();
    if (!transport.freezeBeat) {
        m_beatClock->SetBPM(transport.bpm);
        m_beatClock->Update(static_cast<float>(transport.timeSeconds));
    }
}

void ShaderLabApp::Render() {
    if (!m_initialized) {
        return;
    }

    if (!m_appActive) {
        return;
    }

    // Reset command list
    m_commandQueue->ResetCommandList();
    ID3D12GraphicsCommandList* commandList = m_commandQueue->GetCommandList();

    // Transition backbuffer to render target
    ID3D12Resource* backBuffer = m_swapchain->GetCurrentBackBuffer();
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = backBuffer;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    commandList->ResourceBarrier(1, &barrier);

    // Clear render target
    D3D12_CPU_DESCRIPTOR_HANDLE rtv = m_swapchain->GetCurrentRTV();
    const float clearColor[] = { 0.1f, 0.1f, 0.15f, 1.0f };
    commandList->ClearRenderTargetView(rtv, clearColor, 0, nullptr);

    // Set render target
    commandList->OMSetRenderTargets(1, &rtv, FALSE, nullptr);

    // Set viewport and scissor rect for the backbuffer
    D3D12_VIEWPORT viewport{};
    viewport.Width = static_cast<float>(m_width);
    viewport.Height = static_cast<float>(m_height);
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;
    commandList->RSSetViewports(1, &viewport);

    D3D12_RECT scissor{};
    scissor.right = static_cast<LONG>(m_width);
    scissor.bottom = static_cast<LONG>(m_height);
    commandList->RSSetScissorRects(1, &scissor);

    // Render UI
    m_ui->BeginFrame();
    m_ui->EndFrame();
    m_ui->Render(commandList);

    // Transition backbuffer to present
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
    commandList->ResourceBarrier(1, &barrier);

    // Execute command list
    m_commandQueue->ExecuteCommandList();

    // Present
    const bool previewVsyncEnabled = m_ui ? m_ui->IsPreviewVsyncEnabled() : true;
    m_swapchain->Present(previewVsyncEnabled);

    // Wait for GPU (simple approach for now)
    m_commandQueue->WaitForGPU();
}

void ShaderLabApp::OnResize(uint32_t width, uint32_t height) {
    if (!m_initialized || width == 0 || height == 0) {
        return;
    }

    m_commandQueue->WaitForGPU();
    m_swapchain->Resize(width, height);
    m_width = width;
    m_height = height;
}


LRESULT ShaderLabApp::HandleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_SETTEXT || msg == WM_GETTEXT || msg == WM_GETTEXTLENGTH) {
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }

    bool handled = false;
    switch (msg) {
        case WM_NCCALCSIZE:
            if (m_useCustomTitlebar && wParam) {
                return 0;
            }
            break;

        case WM_NCHITTEST:
            if (m_useCustomTitlebar) {
                POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
                RECT rect = {};
                GetWindowRect(hwnd, &rect);

                const int frameX = GetSystemMetrics(SM_CXFRAME) + GetSystemMetrics(SM_CXPADDEDBORDER);
                const int frameY = GetSystemMetrics(SM_CYFRAME) + GetSystemMetrics(SM_CXPADDEDBORDER);

                const bool onLeft = pt.x < rect.left + frameX;
                const bool onRight = pt.x >= rect.right - frameX;
                const bool onTop = pt.y < rect.top + frameY;
                const bool onBottom = pt.y >= rect.bottom - frameY;

                if (onTop && onLeft) return HTTOPLEFT;
                if (onTop && onRight) return HTTOPRIGHT;
                if (onBottom && onLeft) return HTBOTTOMLEFT;
                if (onBottom && onRight) return HTBOTTOMRIGHT;
                if (onLeft) return HTLEFT;
                if (onRight) return HTRIGHT;
                if (onTop) return HTTOP;
                if (onBottom) return HTBOTTOM;

                const float titlebarHeight = m_ui ? m_ui->GetTitlebarHeight() : 0.0f;
                if (titlebarHeight > 0.0f) {
                    POINT clientPt = pt;
                    ScreenToClient(hwnd, &clientPt);
                    if (clientPt.y >= 0 && clientPt.y < static_cast<LONG>(titlebarHeight)) {
                        return HTCLIENT;
                    }
                }
            }
            break;

        case WM_ACTIVATEAPP:
            m_appActive = (wParam == TRUE);
            handled = true;
            break;

        case WM_SYSCOMMAND:
            if ((wParam & 0xFFF0) == SC_KEYMENU) {
                handled = true;
            }
            break;

        case WM_SIZE:
            if (wParam != SIZE_MINIMIZED) {
                uint32_t width = LOWORD(lParam);
                uint32_t height = HIWORD(lParam);
                OnResize(width, height);
            }
            if (ImGui::GetCurrentContext()) {
                ImGui::ClearActiveID();
                ImGui::GetIO().MouseDown[0] = false;
                ImGui::GetIO().MouseDown[1] = false;
                ImGui::GetIO().MouseDown[2] = false;
            }
            handled = true;
            break;

        case WM_DESTROY:
            PostQuitMessage(0);
            handled = true;
            break;
    }

    // ImGui handles the message after core window state
    if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam)) {
        return true;
    }

    if (handled) {
        return 0;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void ShaderLabApp::RequestRestart(int adapterIndex) {
    if (adapterIndex == m_currentAdapterIndex) {
        return;
    }

    if (m_pendingRestart && adapterIndex == m_pendingAdapterIndex) {
        return;
    }

    m_pendingRestart = true;
    m_pendingAdapterIndex = adapterIndex;
}

void ShaderLabApp::PerformRestart() {
    ProjectState savedState;
    bool hasSavedState = false;
    if (m_ui) {
        savedState = m_ui->CaptureState();
        hasSavedState = true;
    }

    const int previousAdapterIndex = m_currentAdapterIndex;
    const int requestedAdapterIndex = m_pendingAdapterIndex;
    m_pendingRestart = false;

    try {
        if (m_commandQueue) {
            m_commandQueue->WaitForGPU();
        }
    } catch (...) {
    }

    try {
        Shutdown();
    } catch (...) {
    }

    m_currentAdapterIndex = requestedAdapterIndex;
    Sleep(100);

    bool initialized = false;
    try {
        initialized = Initialize(m_hwnd, m_width, m_height);
    } catch (const std::exception&) {
        initialized = false;
    } catch (...) {
        initialized = false;
    }

    if (!initialized && previousAdapterIndex != requestedAdapterIndex) {
        m_currentAdapterIndex = previousAdapterIndex;
        try {
            initialized = Initialize(m_hwnd, m_width, m_height);
        } catch (const std::exception&) {
            initialized = false;
        } catch (...) {
            initialized = false;
        }
    }

    if (initialized && hasSavedState && m_ui) {
        try {
            m_ui->RestoreState(savedState);
        } catch (...) {
        }
    }
}

void ShaderLabApp::UpdateWindowTitle() {
    if (!m_hwnd || !m_ui) {
        return;
    }

    std::string projectName = m_ui->GetProjectName();
    std::string title = "DrCiRCUiT's ShaderLab - For democoders, by a democoder - " + projectName;
    size_t titleNullPos = title.find('\0');
    size_t projectNullPos = projectName.find('\0');
    std::wstring wtitle(title.begin(), title.end());
    if (wtitle.empty()) {
        return;
    }
    if (wtitle != m_lastWindowTitle) {
        SetLastError(0);
        BOOL okW = SetWindowTextW(m_hwnd, wtitle.c_str());
        DWORD errW = GetLastError();

        SetLastError(0);
        BOOL okA = SetWindowTextA(m_hwnd, title.c_str());
        DWORD errA = GetLastError();
        int length = GetWindowTextLengthW(m_hwnd);
        std::wstring readback;
        if (length > 0) {
            readback.resize(static_cast<size_t>(length));
            int readLen = GetWindowTextW(m_hwnd, readback.data(), length + 1);
            if (readLen >= 0 && readLen < length) {
                readback.resize(static_cast<size_t>(readLen));
            }
        }

        if (length <= 1) {
            SetWindowTextW(m_hwnd, wtitle.c_str());
            SetWindowTextA(m_hwnd, title.c_str());
        }

        FILE* f = nullptr;
        const std::string logPath = GetShaderLabLogPath();
        fopen_s(&f, logPath.c_str(), "a");
        if (f) {
            fprintf(f, "Requested title len=%zu nullPos=%zu projectLen=%zu projectNull=%zu\n",
                title.size(), titleNullPos, projectName.size(), projectNullPos);
            fprintf(f, "Requested title str='%s'\n", title.c_str());
            fprintf(f, "SetWindowTextW ok=%d err=%lu SetWindowTextA ok=%d err=%lu\n",
                okW ? 1 : 0, errW, okA ? 1 : 0, errA);
            fwprintf(f, L"Window title set -> len=%d readback='%s'\n", length, readback.c_str());
            fclose(f);
        }
        m_lastWindowTitle = wtitle;
    }
}

} // namespace ShaderLab
