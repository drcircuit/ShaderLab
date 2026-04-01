#pragma once

#include <windows.h>
#include <memory>
#include <string>

namespace ShaderLab {

class Device;
class CommandQueue;
class Swapchain;
class ShaderLabIDE;
class AudioSystem;
class BeatClock;
class ShaderCompiler;
class PreviewRenderer;

class ShaderLabApp {
public:
    ShaderLabApp();
    ~ShaderLabApp();

    bool Initialize(HWND hwnd, uint32_t width, uint32_t height);
    void Shutdown();

    void Update(float deltaTime);
    void Render();

    void OnResize(uint32_t width, uint32_t height);
    LRESULT HandleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    void RequestRestart(int adapterIndex);

private:
    void InitializeGraphics();
    void InitializeAudio();
    void InitializeShaders();
    void PerformRestart();
    void UpdateWindowTitle();
    void ConfigureCustomTitlebar();

    std::unique_ptr<Device> m_device;
    std::unique_ptr<CommandQueue> m_commandQueue;
    std::unique_ptr<Swapchain> m_swapchain;
    std::unique_ptr<ShaderLabIDE> m_ui;
    std::unique_ptr<AudioSystem> m_audio;
    std::unique_ptr<BeatClock> m_beatClock;
    std::unique_ptr<ShaderCompiler> m_shaderCompiler;
    std::unique_ptr<PreviewRenderer> m_previewRenderer;

    HWND m_hwnd = nullptr;
    uint32_t m_width = 0;
    uint32_t m_height = 0;
    bool m_initialized = false;
    std::wstring m_lastWindowTitle;
    bool m_appActive = true;
    bool m_useCustomTitlebar = true;


    // Restart state
    bool m_pendingRestart = false;
    int m_currentAdapterIndex = -1;
    int m_pendingAdapterIndex = -1;
};

} // namespace ShaderLab
