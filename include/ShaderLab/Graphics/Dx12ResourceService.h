#pragma once

#include "ShaderLab/Graphics/ResourceService.h"

namespace ShaderLab {

class Dx12ResourceService final : public IResourceService {
public:
    explicit Dx12ResourceService(ID3D12Device* device) : m_device(device) {}

    bool AllocateTexture(const TextureResourceAllocationRequest& request, ComPtr<ID3D12Resource>& outResource) override;
    bool AllocateTexture2D(const TextureAllocationRequest& request, ComPtr<ID3D12Resource>& outResource) override;
    bool AllocateBuffer(const ResourceBufferAllocationRequest& request, ComPtr<ID3D12Resource>& outResource) override;

private:
    ID3D12Device* m_device = nullptr;
};

} // namespace ShaderLab
