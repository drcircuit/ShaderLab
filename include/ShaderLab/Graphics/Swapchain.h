#pragma once

#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <cstdint>

using Microsoft::WRL::ComPtr;

namespace ShaderLab {

class Device;
class CommandQueue;

class Swapchain {
public:
    static constexpr uint32_t BUFFER_COUNT = 2;

    Swapchain();
    ~Swapchain();

    bool Initialize(Device* device, CommandQueue* commandQueue, 
                   HWND hwnd, uint32_t width, uint32_t height);
    void Shutdown();

    void Present(bool vsync = true);
    void Resize(uint32_t width, uint32_t height);
    bool SupportsTearing() const { return m_allowTearing; }

    ID3D12Resource* GetCurrentBackBuffer() const;
    D3D12_CPU_DESCRIPTOR_HANDLE GetCurrentRTV() const;
    uint32_t GetCurrentBackBufferIndex() const { return m_currentBackBuffer; }

    uint32_t GetWidth() const { return m_width; }
    uint32_t GetHeight() const { return m_height; }

private:
    void CreateRenderTargetViews();
    void ReleaseBackBuffers();

    Device* m_device = nullptr;
    CommandQueue* m_commandQueue = nullptr;

    ComPtr<IDXGISwapChain3> m_swapchain;
    ComPtr<ID3D12DescriptorHeap> m_rtvHeap;
    ComPtr<ID3D12Resource> m_backBuffers[BUFFER_COUNT];

    uint32_t m_width = 0;
    uint32_t m_height = 0;
    uint32_t m_currentBackBuffer = 0;
    uint32_t m_rtvDescriptorSize = 0;
    HWND m_hwnd = nullptr;
    bool m_allowTearing = false;
};

} // namespace ShaderLab
