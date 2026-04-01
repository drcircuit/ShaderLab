#include "ShaderLab/UI/ShaderLabIDE.h"

#include "ShaderLab/Graphics/Device.h"
#include "ShaderLab/Graphics/Dx12ResourceService.h"

#include <cstdint>
#include <cstring>
#include <string>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

namespace ShaderLab {

bool ShaderLabIDE::LoadTextureFromFile(const std::string& path, ComPtr<ID3D12Resource>& outResource) {
    if (path.empty()) return false;

    int w, h, channels;
    unsigned char* data = stbi_load(path.c_str(), &w, &h, &channels, 4);
    if (!data) return false;

    // TODO: Handle failure in creation?
    // Using a local helper for synchronous upload
    CreateTextureFromData(data, w, h, 4, outResource);

    stbi_image_free(data);
    return outResource != nullptr;
}

void ShaderLabIDE::CreateTextureFromData(const void* data, int width, int height, int channels, ComPtr<ID3D12Resource>& outResource) {
    (void)channels;
    auto device = m_deviceRef->GetDevice();
    Dx12ResourceService resourceService(device);

    // 1. Create Default Heap Resource (Dest)
    TextureAllocationRequest textureRequest{};
    textureRequest.width = static_cast<uint32_t>(width);
    textureRequest.height = static_cast<uint32_t>(height);
    textureRequest.format = DXGI_FORMAT_R8G8B8A8_UNORM;
    textureRequest.initialState = D3D12_RESOURCE_STATE_COPY_DEST;
    if (!resourceService.AllocateTexture2D(textureRequest, outResource)) {
        return;
    }

    D3D12_RESOURCE_DESC texDesc = outResource->GetDesc();

    // 2. Create Upload Buffer
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint;
    UINT numRows;
    UINT64 rowSizeInBytes;
    UINT64 totalBytes;

    device->GetCopyableFootprints(&texDesc, 0, 1, 0, &footprint, &numRows, &rowSizeInBytes, &totalBytes);

    ComPtr<ID3D12Resource> uploadBuffer;
    ResourceBufferAllocationRequest uploadRequest{};
    uploadRequest.sizeBytes = totalBytes;
    uploadRequest.heapType = D3D12_HEAP_TYPE_UPLOAD;
    uploadRequest.initialState = D3D12_RESOURCE_STATE_GENERIC_READ;
    if (!resourceService.AllocateBuffer(uploadRequest, uploadBuffer)) {
        return;
    }

    // 3. Copy Memory
    void* mappedData;
    uploadBuffer->Map(0, nullptr, &mappedData);

    const uint8_t* srcData = (const uint8_t*)data;
    uint8_t* dstData = (uint8_t*)mappedData;

    for (UINT i = 0; i < numRows; ++i) {
        memcpy(dstData + footprint.Footprint.RowPitch * i,
               srcData + width * 4 * i,
               width * 4);
    }
    uploadBuffer->Unmap(0, nullptr);

    // 4. Create Short Lived Command Queue/List
    ComPtr<ID3D12CommandQueue> queue;
    D3D12_COMMAND_QUEUE_DESC queueDesc = {};
    queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&queue));

    ComPtr<ID3D12CommandAllocator> allocator;
    device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator));

    ComPtr<ID3D12GraphicsCommandList> cmdList;
    device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr, IID_PPV_ARGS(&cmdList));

    // 5. Record Copy
    D3D12_TEXTURE_COPY_LOCATION dst = {};
    dst.pResource = outResource.Get();
    dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    dst.SubresourceIndex = 0;

    D3D12_TEXTURE_COPY_LOCATION src = {};
    src.pResource = uploadBuffer.Get();
    src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    src.PlacedFootprint = footprint;

    cmdList->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);

    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = outResource.Get();
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

    cmdList->ResourceBarrier(1, &barrier);
    cmdList->Close();

    // 6. Execute and Wait
    ID3D12CommandList* ppCommandLists[] = { cmdList.Get() };
    queue->ExecuteCommandLists(1, ppCommandLists);

    ComPtr<ID3D12Fence> fence;
    device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));
    HANDLE fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);

    queue->Signal(fence.Get(), 1);
    fence->SetEventOnCompletion(1, fenceEvent);
    WaitForSingleObject(fenceEvent, INFINITE);
    CloseHandle(fenceEvent);
}

} // namespace ShaderLab
