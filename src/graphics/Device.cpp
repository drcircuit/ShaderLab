#include "ShaderLab/Graphics/Device.h"

#ifndef SHADERLAB_TINY_PLAYER
#define SHADERLAB_TINY_PLAYER 0
#endif

namespace ShaderLab {

#if SHADERLAB_TINY_PLAYER
static inline void TinyErrCode(const char* code) {
    if (!code) {
        return;
    }
    OutputDebugStringA(code);
    OutputDebugStringA("\n");
}
#endif

Device::Device() = default;
Device::~Device() = default;

std::vector<Device::AdapterInfo> Device::GetAvailableAdapters() {
    std::vector<AdapterInfo> adapters;
    ComPtr<IDXGIFactory4> factory;
    if (FAILED(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory)))) return adapters;

    ComPtr<IDXGIAdapter1> adapter;
    for (UINT i = 0; factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; ++i) {
        DXGI_ADAPTER_DESC1 desc;
        adapter->GetDesc1(&desc);
        
        // Include software adapters? Maybe useful for debugging, but user wants GPU.
        // Let's include everything but mark them or filters in UI?
        // User asked to choose D12 device.
         AdapterInfo info;
        info.name = desc.Description;
        info.videoMemory = desc.DedicatedVideoMemory;
        info.index = i; // Store actual DXGI index
        adapters.push_back(info);
    }
    return adapters;
}

bool Device::Initialize(bool enableValidation, int adapterIndex) {
    m_validationEnabled = enableValidation;
    m_lastInitFailureCode = InitFailureCode::None;

    // Enable debug layer in debug builds
#ifdef SHADERLAB_DEBUG
    if (enableValidation) {
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&m_debugController)))) {
            m_debugController->EnableDebugLayer();
        }
    }
#endif

    // Create DXGI factory
    UINT factoryFlags = 0;
#ifdef SHADERLAB_DEBUG
    if (enableValidation) {
        factoryFlags = DXGI_CREATE_FACTORY_DEBUG;
    }
#endif

    HRESULT hr = CreateDXGIFactory2(factoryFlags, IID_PPV_ARGS(&m_factory));
    if (FAILED(hr)) {
        m_lastInitFailureCode = InitFailureCode::FactoryCreateFailed;
#if SHADERLAB_TINY_PLAYER
        TinyErrCode("E100");
#endif
        return false;
    }

    // Find adapter
    ComPtr<IDXGIAdapter1> adapter;
    
    if (adapterIndex >= 0) {
        // Use specific adapter
        if (FAILED(m_factory->EnumAdapters1((UINT)adapterIndex, &adapter))) {
            m_lastInitFailureCode = InitFailureCode::AdapterSelectionFailed;
#if SHADERLAB_TINY_PLAYER
            TinyErrCode("E101");
#endif
            return false;
        }
        
        // Upgrade interface
        if (FAILED(adapter.As(&m_adapter))) {
            return false;
        }
        m_adapterIndex = adapterIndex;
    } else {
        // Find best adapter (prefer high-performance GPU)
        for (UINT i = 0; m_factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; ++i) {
            DXGI_ADAPTER_DESC1 desc;
            adapter->GetDesc1(&desc);

            // Skip software adapter
            if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) {
                continue;
            }

            // Check if adapter supports D3D12
            if (SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_12_0, 
                                            __uuidof(ID3D12Device), nullptr))) {
                // Found compatible adapter, upgrade interface
                if (FAILED(adapter.As(&m_adapter))) {
                    continue;
                }
                m_adapterIndex = (int)i;
                break;
            }
        }
    }

    if (!m_adapter) {
        m_lastInitFailureCode = InitFailureCode::AdapterSelectionFailed;
#if SHADERLAB_TINY_PLAYER
        TinyErrCode("E101");
#endif
        return false;
    }

    // Get adapter description
    DXGI_ADAPTER_DESC desc;
    m_adapter->GetDesc(&desc);
    m_adapterName = desc.Description;

    // Create D3D12 device
    hr = D3D12CreateDevice(m_adapter.Get(), D3D_FEATURE_LEVEL_12_0, 
                           IID_PPV_ARGS(&m_device));
    if (FAILED(hr)) {
        m_lastInitFailureCode = InitFailureCode::DeviceCreateFailed;
#if SHADERLAB_TINY_PLAYER
        TinyErrCode("E102");
#endif
        return false;
    }

#ifdef SHADERLAB_DEBUG
    // Configure debug info queue
    if (enableValidation) {
        ComPtr<ID3D12InfoQueue> infoQueue;
        if (SUCCEEDED(m_device.As(&infoQueue))) {
            infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, TRUE);
            infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, TRUE);
        }
    }
#endif

    return true;
}

void Device::Shutdown() {
    m_device.Reset();
    m_factory.Reset();
    m_debugController.Reset();
    m_adapter.Reset();
}

Device::MemoryInfo Device::GetVideoMemoryInfo() const {
    MemoryInfo info = {};
    if (m_adapter) {
        DXGI_QUERY_VIDEO_MEMORY_INFO videoMemoryInfo;
        if (SUCCEEDED(m_adapter->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &videoMemoryInfo))) {
            info.usage = videoMemoryInfo.CurrentUsage;
            info.budget = videoMemoryInfo.Budget;
        }
    }
    return info;
}

} // namespace ShaderLab
