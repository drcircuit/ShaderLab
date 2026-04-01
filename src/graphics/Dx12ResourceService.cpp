#include "ShaderLab/Graphics/Dx12ResourceService.h"

namespace ShaderLab {

bool Dx12ResourceService::AllocateTexture(const TextureResourceAllocationRequest& request, ComPtr<ID3D12Resource>& outResource) {
    outResource.Reset();
    if (!m_device || request.width == 0 || request.height == 0 || request.depthOrArraySize == 0) {
        return false;
    }

    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

    D3D12_RESOURCE_DESC texDesc = {};
    texDesc.Dimension = request.dimension;
    texDesc.Width = request.width;
    texDesc.Height = request.height;
    texDesc.DepthOrArraySize = request.depthOrArraySize;
    texDesc.MipLevels = request.mipLevels;
    texDesc.Format = request.format;
    texDesc.SampleDesc = request.sampleDesc;
    texDesc.Layout = request.layout;
    texDesc.Flags = request.flags;

    return SUCCEEDED(m_device->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &texDesc,
        request.initialState,
        nullptr,
        IID_PPV_ARGS(&outResource)));
}

bool Dx12ResourceService::AllocateTexture2D(const TextureAllocationRequest& request, ComPtr<ID3D12Resource>& outResource) {
    TextureResourceAllocationRequest textureRequest = {};
    textureRequest.dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    textureRequest.width = request.width;
    textureRequest.height = request.height;
    textureRequest.depthOrArraySize = 1;
    textureRequest.mipLevels = 1;
    textureRequest.format = request.format;
    textureRequest.sampleDesc = {1, 0};
    textureRequest.layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    textureRequest.flags = request.flags;
    textureRequest.initialState = request.initialState;

    return AllocateTexture(textureRequest, outResource);
}

bool Dx12ResourceService::AllocateBuffer(const ResourceBufferAllocationRequest& request, ComPtr<ID3D12Resource>& outResource) {
    outResource.Reset();
    if (!m_device || request.sizeBytes == 0) {
        return false;
    }

    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = request.heapType;

    D3D12_RESOURCE_DESC bufferDesc = {};
    bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    bufferDesc.Width = request.sizeBytes;
    bufferDesc.Height = 1;
    bufferDesc.DepthOrArraySize = 1;
    bufferDesc.MipLevels = 1;
    bufferDesc.Format = DXGI_FORMAT_UNKNOWN;
    bufferDesc.SampleDesc.Count = 1;
    bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    bufferDesc.Flags = request.flags;

    return SUCCEEDED(m_device->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &bufferDesc,
        request.initialState,
        nullptr,
        IID_PPV_ARGS(&outResource)));
}

} // namespace ShaderLab
