#include "ShaderLab/App/DemoPlayer.h"

#include <algorithm>
#include <cstring>

#include "ShaderLab/Graphics/Device.h"

namespace ShaderLab {

bool CreateRuntimeUavTexture(Device* deviceRef, uint32_t width, uint32_t height, ComPtr<ID3D12Resource>& outTexture);

namespace {

constexpr int kPostFxHistoryCountResources = 4;
constexpr int kMaxPostFxChainResources = 32;
constexpr uint32_t kComputeHistorySlotsResources = 8;

} // namespace

void DemoPlayer::EnsureSceneTexture(int sceneIndex) {
    if (sceneIndex < 0 || sceneIndex >= (int)m_project.scenes.size()) return;
    auto& scene = m_project.scenes[sceneIndex];
    if (m_width == 0 || m_height == 0) return;

    bool needsCreate = !scene.texture;
    if (scene.texture) {
        auto desc = scene.texture->GetDesc();
        if (desc.Width != m_width || desc.Height != m_height) needsCreate = true;
    }

    if (needsCreate) {
        scene.texture.Reset();
        scene.srvHeap.Reset();
        scene.textureValid = false;

        D3D12_HEAP_PROPERTIES heapProps = { D3D12_HEAP_TYPE_DEFAULT };
        D3D12_RESOURCE_DESC texDesc = {};
        texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        texDesc.Width = m_width;
        texDesc.Height = m_height;
        texDesc.DepthOrArraySize = (scene.outputType == TextureType::TextureCube) ? 6 : 1;
        texDesc.MipLevels = 1;
        texDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        texDesc.SampleDesc.Count = 1;
        texDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

        float clearColor[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
        D3D12_CLEAR_VALUE clearValue = {};
        clearValue.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        memcpy(clearValue.Color, clearColor, sizeof(clearColor));

        m_device->GetDevice()->CreateCommittedResource(
            &heapProps, D3D12_HEAP_FLAG_NONE, &texDesc,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
            &clearValue, IID_PPV_ARGS(&scene.texture));

        D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
        heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        heapDesc.NumDescriptors = 8 * kMaxPostFxChainResources;
        heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        m_device->GetDevice()->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&scene.srvHeap));

        D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
        rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        rtvHeapDesc.NumDescriptors = 1;
        rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        m_device->GetDevice()->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&scene.rtvHeap));

        auto rtvHandle = scene.rtvHeap->GetCPUDescriptorHandleForHeapStart();
        m_device->GetDevice()->CreateRenderTargetView(scene.texture.Get(), nullptr, rtvHandle);
    }
}

void DemoPlayer::EnsurePostFxResources(Scene& scene) {
    if (m_width == 0 || m_height == 0 || !m_device) return;

    bool needsCreate = !scene.postFxTextureA || !scene.postFxTextureB;
    if (scene.postFxTextureA) {
        auto desc = scene.postFxTextureA->GetDesc();
        if (desc.Width != m_width || desc.Height != m_height) needsCreate = true;
    }
    if (!needsCreate) return;

    scene.postFxTextureA.Reset();
    scene.postFxTextureB.Reset();
    scene.postFxSrvHeap.Reset();
    scene.postFxRtvHeap.Reset();
    scene.postFxValid = false;

    D3D12_HEAP_PROPERTIES heapProps = { D3D12_HEAP_TYPE_DEFAULT };
    D3D12_RESOURCE_DESC texDesc = {};
    texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    texDesc.Width = m_width;
    texDesc.Height = m_height;
    texDesc.DepthOrArraySize = 1;
    texDesc.MipLevels = 1;
    texDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    texDesc.SampleDesc.Count = 1;
    texDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

    float clearColor[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
    D3D12_CLEAR_VALUE clearValue = {};
    clearValue.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    memcpy(clearValue.Color, clearColor, sizeof(clearColor));

    m_device->GetDevice()->CreateCommittedResource(
        &heapProps, D3D12_HEAP_FLAG_NONE, &texDesc,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
        &clearValue, IID_PPV_ARGS(&scene.postFxTextureA));

    m_device->GetDevice()->CreateCommittedResource(
        &heapProps, D3D12_HEAP_FLAG_NONE, &texDesc,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
        &clearValue, IID_PPV_ARGS(&scene.postFxTextureB));

    D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
    heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    heapDesc.NumDescriptors = 8 * kMaxPostFxChainResources;
    heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    m_device->GetDevice()->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&scene.postFxSrvHeap));

    D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
    rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvHeapDesc.NumDescriptors = 1;
    rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    m_device->GetDevice()->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&scene.postFxRtvHeap));
}

void DemoPlayer::EnsurePostFxHistory(Scene::PostFXEffect& effect) {
    if (!m_device || m_width == 0 || m_height == 0) return;

    bool needsCreate = (int)effect.historyTextures.size() != kPostFxHistoryCountResources;
    if (!needsCreate) {
        auto desc = effect.historyTextures[0]->GetDesc();
        if (desc.Width != m_width || desc.Height != m_height) needsCreate = true;
    }

    if (!needsCreate) return;

    effect.historyTextures.clear();
    effect.historyTextures.resize(kPostFxHistoryCountResources);
    effect.historyIndex = 0;
    effect.historyInitialized = false;

    D3D12_HEAP_PROPERTIES heapProps = { D3D12_HEAP_TYPE_DEFAULT };
    D3D12_RESOURCE_DESC texDesc = {};
    texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    texDesc.Width = m_width;
    texDesc.Height = m_height;
    texDesc.DepthOrArraySize = 1;
    texDesc.MipLevels = 1;
    texDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    texDesc.SampleDesc.Count = 1;
    texDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

    for (int i = 0; i < kPostFxHistoryCountResources; ++i) {
        m_device->GetDevice()->CreateCommittedResource(
            &heapProps, D3D12_HEAP_FLAG_NONE, &texDesc,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
            nullptr, IID_PPV_ARGS(&effect.historyTextures[i]));
    }
}

void DemoPlayer::EnsureComputeHistory(Scene::ComputeEffect& effect) {
#if SHADERLAB_TINY_PLAYER
    (void)effect;
    return;
#else
    if (!m_device || m_width == 0 || m_height == 0) return;
    const int historyCount = (std::max)(0, (std::min)(effect.historyCount, static_cast<int>(kComputeHistorySlotsResources)));
    if (historyCount <= 0) {
        effect.historyTextures.clear();
        effect.historyIndex = 0;
        effect.historyInitialized = false;
        return;
    }

    bool needsCreate = static_cast<int>(effect.historyTextures.size()) != historyCount;
    if (!needsCreate && !effect.historyTextures.empty()) {
        const auto desc = effect.historyTextures.front()->GetDesc();
        needsCreate = desc.Width != m_width || desc.Height != m_height;
    }
    if (!needsCreate) return;

    effect.historyTextures.clear();
    effect.historyTextures.resize(static_cast<size_t>(historyCount));
    effect.historyIndex = 0;
    effect.historyInitialized = false;

    for (int i = 0; i < historyCount; ++i) {
        if (!CreateRuntimeUavTexture(m_device, m_width, m_height, effect.historyTextures[static_cast<size_t>(i)])) {
            effect.historyTextures.clear();
            effect.historyIndex = 0;
            effect.historyInitialized = false;
            return;
        }
    }
#endif
}

}