#include "ShaderLab/UI/ShaderLabIDE.h"

#include <cmath>

#include "ShaderLab/Graphics/Device.h"
#include "ShaderLab/Graphics/Dx12ResourceService.h"
#include "ShaderLab/Graphics/PreviewRenderer.h"

namespace ShaderLab {

namespace {

void ComputeShaderMusicalTimingSceneGraph(const PreviewTransport& transport,
                                          float& outIBeat,
                                          float& outIBar,
                                          float& outFBeat,
                                          float& outFBarBeat,
                                          float& outFBarBeat16) {
    constexpr float kBeatsPerBar = 4.0f;
    constexpr float kSixteenthPerBeat = 4.0f;
    const float beatsPerSecond = transport.bpm / 60.0f;
    float exactBeat = 0.0f;
    if (beatsPerSecond > 0.0f) {
        exactBeat = static_cast<float>(transport.timeSeconds * static_cast<double>(beatsPerSecond));
        if (exactBeat < 0.0f) {
            exactBeat = 0.0f;
        }
    }
    const float beat = std::floor(exactBeat);
    const float bar = std::floor(beat / kBeatsPerBar);
    const float beatInBar = exactBeat - std::floor(exactBeat / kBeatsPerBar) * kBeatsPerBar;
    float barBeat16 = std::floor(beatInBar * kSixteenthPerBeat);
    if (barBeat16 < 0.0f) {
        barBeat16 = 0.0f;
    }
    if (barBeat16 > 15.0f) {
        barBeat16 = 15.0f;
    }

    outIBeat = beat;
    outIBar = bar;
    outFBeat = exactBeat;
    outFBarBeat = beatInBar;
    outFBarBeat16 = barBeat16;
}

} // namespace

void ShaderLabIDE::EnsureSceneTexture(int sceneIndex, uint32_t width, uint32_t height) {
    if (sceneIndex < 0 || sceneIndex >= m_scenes.size()) return;
    auto& scene = m_scenes[sceneIndex];
    if (width == 0 || height == 0) return;

    bool needsCreate = !scene.texture;
    if (scene.texture) {
        auto desc = scene.texture->GetDesc();
        if (desc.Width != width || desc.Height != height) {
            needsCreate = true;
        }
    }

    if (scene.srvHeap && scene.srvHeap->GetDesc().NumDescriptors != 8) {
        scene.srvHeap.Reset();
        needsCreate = true;
        scene.texture.Reset();
        scene.textureValid = false;
    }

    if (needsCreate) {
        Dx12ResourceService resourceService(m_deviceRef->GetDevice());
        scene.texture.Reset();
        scene.srvHeap.Reset();
        scene.rtvHeap.Reset();
        scene.textureValid = false;

        TextureAllocationRequest textureRequest{};
        textureRequest.width = width;
        textureRequest.height = height;
        textureRequest.format = DXGI_FORMAT_R8G8B8A8_UNORM;
        textureRequest.flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        textureRequest.initialState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        if (!resourceService.AllocateTexture2D(textureRequest, scene.texture)) {
            return;
        }

        D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
        heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        heapDesc.NumDescriptors = 8;
        heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        m_deviceRef->GetDevice()->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&scene.srvHeap));

        D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
        rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        rtvHeapDesc.NumDescriptors = 1;
        rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        m_deviceRef->GetDevice()->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&scene.rtvHeap));

        if (scene.rtvHeap) {
            D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = scene.rtvHeap->GetCPUDescriptorHandleForHeapStart();
            m_deviceRef->GetDevice()->CreateRenderTargetView(scene.texture.Get(), nullptr, rtvHandle);
        }
    }
}

ID3D12Resource* ShaderLabIDE::GetSceneFinalTexture(ID3D12GraphicsCommandList* commandList,
                                                   int sceneIndex,
                                                   uint32_t width,
                                                   uint32_t height,
                                                   double timeSeconds) {
    if (sceneIndex < 0 || sceneIndex >= static_cast<int>(m_scenes.size())) return nullptr;
    RenderScene(commandList, sceneIndex, width, height, timeSeconds);
    auto& scene = m_scenes[sceneIndex];
    if (!scene.texture) return nullptr;

    ID3D12Resource* output = scene.texture.Get();

    if (!scene.postFxChain.empty()) {
        output = ApplyPostFxChain(commandList, scene, scene.postFxChain, output, width, height, timeSeconds, false);
    }

    if (!scene.computeEffectChain.empty()) {
        output = ApplyComputeEffectChain(commandList, sceneIndex, scene.computeEffectChain, output, width, height, timeSeconds);
    }

    return output;
}

void ShaderLabIDE::RenderScene(ID3D12GraphicsCommandList* commandList,
                               int sceneIndex,
                               uint32_t width,
                               uint32_t height,
                               double time) {
    for (int stackSceneIndex : m_renderStack) {
        if (stackSceneIndex == sceneIndex) return;
    }
    m_renderStack.push_back(sceneIndex);

    EnsureSceneTexture(sceneIndex, width, height);
    if (sceneIndex < 0 || sceneIndex >= static_cast<int>(m_scenes.size())) {
        m_renderStack.pop_back();
        return;
    }

    auto& scene = m_scenes[sceneIndex];
    if (!scene.texture) {
        m_renderStack.pop_back();
        return;
    }

    for (const auto& binding : scene.bindings) {
        if (binding.enabled && binding.sourceSceneIndex != -1 && binding.sourceSceneIndex != sceneIndex) {
            RenderScene(commandList, binding.sourceSceneIndex, width, height, time);
        }
    }

    PopulateSceneBindingDescriptors(sceneIndex, scene);

#if SHADERLAB_DEBUG
    if (m_dbgEnableAutoCompile && (scene.isDirty || !scene.pipelineState)) {
#else
    if (scene.isDirty || !scene.pipelineState) {
#endif
        CompileScene(sceneIndex);
        scene.isDirty = false;
    }

    if (!scene.rtvHeap) {
        D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
        rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        rtvHeapDesc.NumDescriptors = 1;
        rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        m_deviceRef->GetDevice()->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&scene.rtvHeap));
        if (scene.rtvHeap) {
            D3D12_CPU_DESCRIPTOR_HANDLE initHandle = scene.rtvHeap->GetCPUDescriptorHandleForHeapStart();
            m_deviceRef->GetDevice()->CreateRenderTargetView(scene.texture.Get(), nullptr, initHandle);
        }
    }

    if (!scene.rtvHeap) {
        m_renderStack.pop_back();
        return;
    }

    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = scene.rtvHeap->GetCPUDescriptorHandleForHeapStart();

    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = scene.texture.Get();
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    commandList->ResourceBarrier(1, &barrier);

    if (scene.pipelineState) {
        if (scene.srvHeap) {
            ID3D12DescriptorHeap* heaps[] = { scene.srvHeap.Get() };
            commandList->SetDescriptorHeaps(1, heaps);
        }

        float iBeat = 0.0f;
        float iBar = 0.0f;
        float fBeat = 0.0f;
        float fBarBeat = 0.0f;
        float fBarBeat16 = 0.0f;
        ComputeShaderMusicalTimingSceneGraph(m_transport, iBeat, iBar, fBeat, fBarBeat, fBarBeat16);
        m_previewRenderer->Render(
            commandList,
            scene.pipelineState.Get(),
            scene.texture.Get(),
            rtvHandle,
            scene.srvHeap ? scene.srvHeap->GetGPUDescriptorHandleForHeapStart() : D3D12_GPU_DESCRIPTOR_HANDLE{},
            width,
            height,
            static_cast<float>(time),
            iBeat,
            iBar,
            fBarBeat16,
            fBeat,
            fBarBeat);
    }

    std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
    commandList->ResourceBarrier(1, &barrier);

    scene.textureValid = true;
    m_renderStack.pop_back();
}

}