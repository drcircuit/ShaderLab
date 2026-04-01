#pragma once

#include <cstdint>

#include <d3d12.h>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

namespace ShaderLab {

struct BufferAllocationRequest {
    uint64_t sizeBytes = 0;
    D3D12_HEAP_TYPE heapType = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_STATES initialState = D3D12_RESOURCE_STATE_COMMON;
    D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE;
};

class IGraphicsDeviceService {
public:
    virtual ~IGraphicsDeviceService() = default;

    virtual ID3D12Device* DeviceHandle() const = 0;
    virtual ID3D12CommandQueue* GraphicsQueueHandle() const = 0;
    virtual bool AllocateBuffer(const BufferAllocationRequest& request, ComPtr<ID3D12Resource>& outResource) = 0;
};

} // namespace ShaderLab
