#include "ShaderLab/UI/ShaderLabIDE.h"

#include <algorithm>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

#include "ShaderLab/Graphics/Device.h"
#include "ShaderLab/Graphics/Dx12ResourceService.h"

namespace ShaderLab {

namespace fs = std::filesystem;

void ShaderLabIDE::CreateTitlebarIconTexture() {
    m_titlebarIconTexture.Reset();
    m_titlebarIconSrvGpuHandle = {};

    if (!m_deviceRef || !m_srvHeap) {
        return;
    }

    fs::path iconPath;
    const fs::path preferred = fs::path(m_appRoot) / "editor_assets" / "shaderlab.ico.ico";
    if (fs::exists(preferred)) {
        iconPath = preferred;
    } else {
        const fs::path iconDir = fs::path(m_appRoot) / "editor_assets";
        std::error_code ec;
        if (fs::exists(iconDir, ec)) {
            for (const auto& entry : fs::directory_iterator(iconDir, ec)) {
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

    if (iconPath.empty()) {
        return;
    }

    HICON icon = static_cast<HICON>(LoadImageW(
        nullptr,
        iconPath.wstring().c_str(),
        IMAGE_ICON,
        32,
        32,
        LR_LOADFROMFILE));
    if (!icon) {
        return;
    }

    ICONINFO iconInfo = {};
    if (!GetIconInfo(icon, &iconInfo)) {
        DestroyIcon(icon);
        return;
    }

    BITMAP bitmap = {};
    GetObject(iconInfo.hbmColor ? iconInfo.hbmColor : iconInfo.hbmMask, sizeof(bitmap), &bitmap);
    int width = (bitmap.bmWidth > 0) ? bitmap.bmWidth : 32;
    int height = (bitmap.bmHeight > 0) ? bitmap.bmHeight : 32;
    if (!iconInfo.hbmColor && height > 1) {
        height /= 2;
    }
    width = (std::max)(16, width);
    height = (std::max)(16, height);

    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = width;
    bmi.bmiHeader.biHeight = -height;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    HDC hdc = CreateCompatibleDC(nullptr);
    void* dibPixels = nullptr;
    HBITMAP dib = CreateDIBSection(hdc, &bmi, DIB_RGB_COLORS, &dibPixels, nullptr, 0);
    HGDIOBJ oldObject = SelectObject(hdc, dib);
    PatBlt(hdc, 0, 0, width, height, BLACKNESS);
    DrawIconEx(hdc, 0, 0, icon, width, height, 0, nullptr, DI_NORMAL);

    std::vector<unsigned char> rgba(static_cast<size_t>(width) * static_cast<size_t>(height) * 4u, 0);
    if (dibPixels) {
        const unsigned char* bgra = static_cast<const unsigned char*>(dibPixels);
        for (size_t i = 0; i < static_cast<size_t>(width) * static_cast<size_t>(height); ++i) {
            rgba[i * 4 + 0] = bgra[i * 4 + 2];
            rgba[i * 4 + 1] = bgra[i * 4 + 1];
            rgba[i * 4 + 2] = bgra[i * 4 + 0];
            rgba[i * 4 + 3] = bgra[i * 4 + 3];
        }
    }

    SelectObject(hdc, oldObject);
    DeleteObject(dib);
    DeleteDC(hdc);
    if (iconInfo.hbmColor) {
        DeleteObject(iconInfo.hbmColor);
    }
    if (iconInfo.hbmMask) {
        DeleteObject(iconInfo.hbmMask);
    }
    DestroyIcon(icon);

    CreateTextureFromData(rgba.data(), width, height, 4, m_titlebarIconTexture);
    if (!m_titlebarIconTexture) {
        return;
    }

    constexpr UINT kTitlebarIconSrvIndex = 127;
    const UINT descriptorSize = m_deviceRef->GetDevice()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle = m_srvHeap->GetCPUDescriptorHandleForHeapStart();
    cpuHandle.ptr += static_cast<SIZE_T>(kTitlebarIconSrvIndex) * descriptorSize;

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Texture2D.MipLevels = 1;
    m_deviceRef->GetDevice()->CreateShaderResourceView(m_titlebarIconTexture.Get(), &srvDesc, cpuHandle);

    m_titlebarIconSrvGpuHandle = m_srvHeap->GetGPUDescriptorHandleForHeapStart();
    m_titlebarIconSrvGpuHandle.ptr += static_cast<SIZE_T>(kTitlebarIconSrvIndex) * descriptorSize;
}

void ShaderLabIDE::CreatePreviewTexture(uint32_t width, uint32_t height) {
    if (!m_deviceRef || width == 0 || height == 0) {
        return;
    }

    // Only recreate if size changed
    if (m_previewTexture && m_previewTextureWidth == width && m_previewTextureHeight == height) {
        return;
    }

    m_previewTexture.Reset();
    m_previewRtvHeap.Reset();

    // Create render target texture
    Dx12ResourceService resourceService(m_deviceRef->GetDevice());
    TextureAllocationRequest textureRequest = {};
    textureRequest.width = width;
    textureRequest.height = height;
    textureRequest.format = DXGI_FORMAT_R8G8B8A8_UNORM;
    textureRequest.flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    textureRequest.initialState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    if (!resourceService.AllocateTexture2D(textureRequest, m_previewTexture)) {
        return;
    }

    // Create RTV heap
    D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
    rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvHeapDesc.NumDescriptors = 1;
    m_deviceRef->GetDevice()->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&m_previewRtvHeap));

    // Create RTV
    m_previewRtvHandle = m_previewRtvHeap->GetCPUDescriptorHandleForHeapStart();
    m_deviceRef->GetDevice()->CreateRenderTargetView(m_previewTexture.Get(), nullptr, m_previewRtvHandle);

    // Create SRV in ImGui's descriptor heap (descriptor index 1, after ImGui's font texture at 0)
    if (m_srvHeap) {
        UINT descriptorSize = m_deviceRef->GetDevice()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        D3D12_CPU_DESCRIPTOR_HANDLE srvHandle = m_srvHeap->GetCPUDescriptorHandleForHeapStart();
        srvHandle.ptr += descriptorSize;  // Skip ImGui's font texture descriptor

        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Texture2D.MipLevels = 1;
        m_deviceRef->GetDevice()->CreateShaderResourceView(
            m_previewTexture.Get(),
            &srvDesc,
            srvHandle
        );

        // Store GPU handle for ImGui::Image
        D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle = m_srvHeap->GetGPUDescriptorHandleForHeapStart();
        gpuHandle.ptr += descriptorSize;
        m_previewSrvGpuHandle = gpuHandle;
    }

    m_previewTextureWidth = width;
    m_previewTextureHeight = height;

    // Also create dummy texture if not exists
    CreateDummyTexture();
}

void ShaderLabIDE::CreateDummyTexture() {
    auto device = m_deviceRef->GetDevice();
    Dx12ResourceService resourceService(device);

    // 1. Texture2D Dummy
    if (!m_dummyTexture) {
        // Initialize to solid black
        unsigned char blackPixel[4] = {0,0,0,255};
        CreateTextureFromData(blackPixel, 1, 1, 4, m_dummyTexture);

        D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
        heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        heapDesc.NumDescriptors = 1;
        heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&m_dummySrvHeap));

        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Texture2D.MipLevels = 1;
        device->CreateShaderResourceView(m_dummyTexture.Get(), &srvDesc, m_dummySrvHeap->GetCPUDescriptorHandleForHeapStart());
    }

    // 2. TextureCube Dummy
    if (!m_dummyTextureCube) {
        TextureResourceAllocationRequest cubeRequest = {};
        cubeRequest.dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        cubeRequest.width = 1;
        cubeRequest.height = 1;
        cubeRequest.depthOrArraySize = 6;
        cubeRequest.mipLevels = 1;
        cubeRequest.format = DXGI_FORMAT_R8G8B8A8_UNORM;
        cubeRequest.sampleDesc = {1, 0};
        cubeRequest.layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        cubeRequest.flags = D3D12_RESOURCE_FLAG_NONE;
        cubeRequest.initialState = D3D12_RESOURCE_STATE_COMMON;

        if (!resourceService.AllocateTexture(cubeRequest, m_dummyTextureCube)) {
            return;
        }

        D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
        heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        heapDesc.NumDescriptors = 1;
        heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&m_dummySrvHeapCube));

        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE; // Cube view
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.TextureCube.MipLevels = 1;
        device->CreateShaderResourceView(m_dummyTextureCube.Get(), &srvDesc, m_dummySrvHeapCube->GetCPUDescriptorHandleForHeapStart());
    }

    // 3. Texture3D Dummy
    if (!m_dummyTexture3D) {
        TextureResourceAllocationRequest volumeRequest = {};
        volumeRequest.dimension = D3D12_RESOURCE_DIMENSION_TEXTURE3D;
        volumeRequest.width = 1;
        volumeRequest.height = 1;
        volumeRequest.depthOrArraySize = 1;
        volumeRequest.mipLevels = 1;
        volumeRequest.format = DXGI_FORMAT_R8G8B8A8_UNORM;
        volumeRequest.sampleDesc = {1, 0};
        volumeRequest.layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        volumeRequest.flags = D3D12_RESOURCE_FLAG_NONE;
        volumeRequest.initialState = D3D12_RESOURCE_STATE_COMMON;

        if (!resourceService.AllocateTexture(volumeRequest, m_dummyTexture3D)) {
            return;
        }

        D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
        heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        heapDesc.NumDescriptors = 1;
        heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&m_dummySrvHeap3D));

        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE3D; // 3D view
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Texture3D.MipLevels = 1;
        device->CreateShaderResourceView(m_dummyTexture3D.Get(), &srvDesc, m_dummySrvHeap3D->GetCPUDescriptorHandleForHeapStart());
    }
}

} // namespace ShaderLab
