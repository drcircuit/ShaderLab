#include "ShaderLab/Graphics/Swapchain.h"
#include "ShaderLab/Graphics/Device.h"
#include "ShaderLab/Graphics/CommandQueue.h"

namespace ShaderLab {

Swapchain::Swapchain() = default;

Swapchain::~Swapchain() {
    Shutdown();
}

bool Swapchain::Initialize(Device* device, CommandQueue* commandQueue,
                           HWND hwnd, uint32_t width, uint32_t height) {
    if (!device || !device->IsValid() || !commandQueue || !hwnd) {
        return false;
    }

    m_device = device;
    m_commandQueue = commandQueue;
    m_width = width;
    m_height = height;
    m_hwnd = hwnd;
    m_allowTearing = false;

    ComPtr<IDXGIFactory5> factory5;
    if (SUCCEEDED(device->GetFactory()->QueryInterface(IID_PPV_ARGS(&factory5)))) {
        BOOL allowTearing = FALSE;
        if (SUCCEEDED(factory5->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &allowTearing, sizeof(allowTearing))) && allowTearing) {
            m_allowTearing = true;
        }
    }

    // Create swapchain
    DXGI_SWAP_CHAIN_DESC1 swapchainDesc = {};
    swapchainDesc.Width = width;
    swapchainDesc.Height = height;
    swapchainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    swapchainDesc.Stereo = FALSE;
    swapchainDesc.SampleDesc.Count = 1;
    swapchainDesc.SampleDesc.Quality = 0;
    swapchainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapchainDesc.BufferCount = BUFFER_COUNT;
    swapchainDesc.Scaling = DXGI_SCALING_STRETCH;
    swapchainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    swapchainDesc.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;
    swapchainDesc.Flags = m_allowTearing ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;

    ComPtr<IDXGISwapChain1> swapchain1;
    HRESULT hr = device->GetFactory()->CreateSwapChainForHwnd(
        commandQueue->GetQueue(),
        hwnd,
        &swapchainDesc,
        nullptr,
        nullptr,
        &swapchain1
    );

    if (FAILED(hr)) {
        return false;
    }

    hr = swapchain1.As(&m_swapchain);
    if (FAILED(hr)) {
        return false;
    }

    // Disable Alt+Enter fullscreen
    device->GetFactory()->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER);

    m_currentBackBuffer = m_swapchain->GetCurrentBackBufferIndex();

    // Create RTV descriptor heap
    D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
    rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvHeapDesc.NumDescriptors = BUFFER_COUNT;
    rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

    hr = device->GetDevice()->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&m_rtvHeap));
    if (FAILED(hr)) {
        return false;
    }

    m_rtvDescriptorSize = device->GetDevice()->GetDescriptorHandleIncrementSize(
        D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    CreateRenderTargetViews();

    return true;
}

void Swapchain::Shutdown() {
    ReleaseBackBuffers();
    m_rtvHeap.Reset();
    m_swapchain.Reset();
    m_hwnd = nullptr;
}

void Swapchain::Present(bool vsync) {
    UINT syncInterval = vsync ? 1 : 0;
    UINT presentFlags = (!vsync && m_allowTearing) ? DXGI_PRESENT_ALLOW_TEARING : 0;
    m_swapchain->Present(syncInterval, presentFlags);
    m_currentBackBuffer = m_swapchain->GetCurrentBackBufferIndex();
}

void Swapchain::Resize(uint32_t width, uint32_t height) {
    if (!m_swapchain || (width == m_width && height == m_height)) {
        return;
    }

    m_commandQueue->WaitForGPU();

    ReleaseBackBuffers();

    HRESULT hr = m_swapchain->ResizeBuffers(
        BUFFER_COUNT,
        width,
        height,
        DXGI_FORMAT_R8G8B8A8_UNORM,
        m_allowTearing ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0
    );

    if (SUCCEEDED(hr)) {
        m_width = width;
        m_height = height;
        m_currentBackBuffer = m_swapchain->GetCurrentBackBufferIndex();
        CreateRenderTargetViews();
    }
}

ID3D12Resource* Swapchain::GetCurrentBackBuffer() const {
    return m_backBuffers[m_currentBackBuffer].Get();
}

D3D12_CPU_DESCRIPTOR_HANDLE Swapchain::GetCurrentRTV() const {
    D3D12_CPU_DESCRIPTOR_HANDLE handle = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
    handle.ptr += m_currentBackBuffer * m_rtvDescriptorSize;
    return handle;
}

void Swapchain::CreateRenderTargetViews() {
    if (!m_device || !m_swapchain || !m_rtvHeap) {
        return;
    }

    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();

    for (uint32_t i = 0; i < BUFFER_COUNT; ++i) {
        m_swapchain->GetBuffer(i, IID_PPV_ARGS(&m_backBuffers[i]));
        m_device->GetDevice()->CreateRenderTargetView(m_backBuffers[i].Get(), 
                                                      nullptr, rtvHandle);
        rtvHandle.ptr += m_rtvDescriptorSize;
    }
}

void Swapchain::ReleaseBackBuffers() {
    for (uint32_t i = 0; i < BUFFER_COUNT; ++i) {
        m_backBuffers[i].Reset();
    }
}

} // namespace ShaderLab
