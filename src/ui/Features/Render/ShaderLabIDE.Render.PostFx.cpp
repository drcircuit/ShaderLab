#include "ShaderLab/UI/ShaderLabIDE.h"
#include "ShaderLab/UI/UISystemAssets.h"

#include "ShaderLab/Core/CompilationService.h"
#include "ShaderLab/Graphics/Device.h"
#include "ShaderLab/Graphics/Dx12ResourceService.h"
#include "ShaderLab/Graphics/PreviewRenderer.h"

#include <algorithm>
#include <cmath>

namespace ShaderLab {

namespace {

void ComputeShaderMusicalTimingPostFx(const PreviewTransport& transport,
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

void ShaderLabIDE::EnsurePostFxResources(Scene& scene, uint32_t width, uint32_t height) {
    if (!m_deviceRef || width == 0 || height == 0) return;

    bool needsCreate = !scene.postFxTextureA || !scene.postFxTextureB;
    if (scene.postFxTextureA) {
        auto desc = scene.postFxTextureA->GetDesc();
        if (desc.Width != width || desc.Height != height) needsCreate = true;
    }

    if (!needsCreate) return;

    scene.postFxTextureA.Reset();
    scene.postFxTextureB.Reset();
    scene.postFxSrvHeap.Reset();
    scene.postFxRtvHeap.Reset();
    scene.postFxValid = false;

    Dx12ResourceService resourceService(m_deviceRef->GetDevice());
    TextureAllocationRequest textureRequest{};
    textureRequest.width = width;
    textureRequest.height = height;
    textureRequest.format = DXGI_FORMAT_R8G8B8A8_UNORM;
    textureRequest.flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    textureRequest.initialState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;

    if (!resourceService.AllocateTexture2D(textureRequest, scene.postFxTextureA)) {
        return;
    }
    if (!resourceService.AllocateTexture2D(textureRequest, scene.postFxTextureB)) {
        scene.postFxTextureA.Reset();
        return;
    }

    D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
    heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    heapDesc.NumDescriptors = 8 * kMaxPostFxChain;
    heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    m_deviceRef->GetDevice()->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&scene.postFxSrvHeap));

    D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
    rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvHeapDesc.NumDescriptors = 1;
    rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    m_deviceRef->GetDevice()->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&scene.postFxRtvHeap));
}

void ShaderLabIDE::EnsurePostFxPreviewResources(uint32_t width, uint32_t height) {
    if (!m_deviceRef || width == 0 || height == 0) return;

    bool needsCreate = !m_postFxPreviewTextureA || !m_postFxPreviewTextureB;
    if (m_postFxPreviewTextureA) {
        auto desc = m_postFxPreviewTextureA->GetDesc();
        if (desc.Width != width || desc.Height != height) needsCreate = true;
    }
    if (!needsCreate) return;

    m_postFxPreviewTextureA.Reset();
    m_postFxPreviewTextureB.Reset();
    m_postFxPreviewSrvHeap.Reset();
    m_postFxPreviewRtvHeap.Reset();

    Dx12ResourceService resourceService(m_deviceRef->GetDevice());
    TextureAllocationRequest textureRequest{};
    textureRequest.width = width;
    textureRequest.height = height;
    textureRequest.format = DXGI_FORMAT_R8G8B8A8_UNORM;
    textureRequest.flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    textureRequest.initialState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;

    if (!resourceService.AllocateTexture2D(textureRequest, m_postFxPreviewTextureA)) {
        return;
    }
    if (!resourceService.AllocateTexture2D(textureRequest, m_postFxPreviewTextureB)) {
        m_postFxPreviewTextureA.Reset();
        return;
    }

    D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
    heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    heapDesc.NumDescriptors = 8 * kMaxPostFxChain;
    heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    m_deviceRef->GetDevice()->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&m_postFxPreviewSrvHeap));

    D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
    rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvHeapDesc.NumDescriptors = 1;
    rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    m_deviceRef->GetDevice()->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&m_postFxPreviewRtvHeap));

    m_postFxPreviewWidth = width;
    m_postFxPreviewHeight = height;
}

void ShaderLabIDE::EnsurePostFxHistory(Scene::PostFXEffect& effect, uint32_t width, uint32_t height) {
    if (!m_deviceRef || width == 0 || height == 0) return;

    bool needsCreate = static_cast<int>(effect.historyTextures.size()) != kPostFxHistoryCount;
    if (!needsCreate) {
        auto desc = effect.historyTextures[0]->GetDesc();
        if (desc.Width != width || desc.Height != height) needsCreate = true;
    }

    if (!needsCreate) return;

    effect.historyTextures.clear();
    effect.historyTextures.resize(kPostFxHistoryCount);
    effect.historyIndex = 0;
    effect.historyInitialized = false;

    Dx12ResourceService resourceService(m_deviceRef->GetDevice());
    TextureAllocationRequest textureRequest{};
    textureRequest.width = width;
    textureRequest.height = height;
    textureRequest.format = DXGI_FORMAT_R8G8B8A8_UNORM;
    textureRequest.flags = D3D12_RESOURCE_FLAG_NONE;
    textureRequest.initialState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;

    for (int i = 0; i < kPostFxHistoryCount; ++i) {
        if (!resourceService.AllocateTexture2D(textureRequest, effect.historyTextures[i])) {
            effect.historyTextures.clear();
            effect.historyInitialized = false;
            return;
        }
    }
}

bool ShaderLabIDE::CompilePostFxEffect(Scene::PostFXEffect& effect, std::vector<std::string>& outErrors) {
    outErrors.clear();
    if (!m_previewRenderer || !m_compilationService) return false;

    std::vector<CompilationTextureBinding> bindings = { {0, "Texture2D"} };
    const ShaderCompileResult compileResult = m_compilationService->CompilePreviewShader(
        effect.shaderCode,
        bindings,
        true,
        "main",
        L"postfx.hlsl",
        ShaderCompileMode::Live);

    for (const auto& diagnostic : compileResult.diagnostics) {
        outErrors.push_back(diagnostic.message);
    }

    ComPtr<ID3D12PipelineState> pso;
    if (compileResult.success) {
        pso = m_previewRenderer->CreatePSOFromBytecode(compileResult.bytecode);
        if (!pso) {
            outErrors.push_back("Failed to create graphics pipeline state from compiled post-fx shader.");
        }
    }

    if (pso) {
        effect.pipelineState = pso;
        effect.compiledShaderBytes = compileResult.bytecode.size();
        effect.isDirty = false;
        effect.lastCompiledCode = effect.shaderCode;
        return true;
    }
    effect.pipelineState = nullptr;
    effect.compiledShaderBytes = 0;
    return false;
}

ID3D12Resource* ShaderLabIDE::ApplyPostFxChain(ID3D12GraphicsCommandList* commandList,
                                                Scene& scene,
                                                std::vector<Scene::PostFXEffect>& chain,
                                                ID3D12Resource* inputTexture,
                                                uint32_t width,
                                                uint32_t height,
                                                double timeSeconds,
                                                bool usePreviewResources) {
    if (!commandList || !inputTexture) return inputTexture;

    bool anyEnabled = false;
    for (const auto& fx : chain) {
        if (fx.enabled) { anyEnabled = true; break; }
    }
    if (!anyEnabled) return inputTexture;

    ID3D12Resource* ping = nullptr;
    ID3D12Resource* pong = nullptr;
    ID3D12DescriptorHeap* srvHeap = nullptr;
    ID3D12DescriptorHeap* rtvHeap = nullptr;

    if (usePreviewResources) {
        EnsurePostFxPreviewResources(width, height);
        ping = m_postFxPreviewTextureA.Get();
        pong = m_postFxPreviewTextureB.Get();
        srvHeap = m_postFxPreviewSrvHeap.Get();
        rtvHeap = m_postFxPreviewRtvHeap.Get();
    } else {
        EnsurePostFxResources(scene, width, height);
        ping = scene.postFxTextureA.Get();
        pong = scene.postFxTextureB.Get();
        srvHeap = scene.postFxSrvHeap.Get();
        rtvHeap = scene.postFxRtvHeap.Get();
    }

    if (!ping || !pong || !srvHeap || !rtvHeap) return inputTexture;

    auto device = m_deviceRef->GetDevice();
    auto handleStep = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    auto startHandle = srvHeap->GetCPUDescriptorHandleForHeapStart();

    auto bindInput = [&](ID3D12Resource* src, Scene::PostFXEffect& fx, int baseSlot) {
        D3D12_CPU_DESCRIPTOR_HANDLE dest = startHandle;
        dest.ptr += baseSlot * handleStep;
        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MipLevels = 1;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        device->CreateShaderResourceView(src, &srvDesc, dest);

        for (int i = 1; i <= kPostFxHistoryCount; ++i) {
            int historySlot = i;
            int historyIndex = fx.historyIndex - (i - 1);
            while (historyIndex < 0) historyIndex += kPostFxHistoryCount;
            ID3D12Resource* historyRes = nullptr;
            if (!fx.historyTextures.empty()) {
                historyRes = fx.historyTextures[historyIndex].Get();
            }

            D3D12_CPU_DESCRIPTOR_HANDLE histDest = startHandle;
            histDest.ptr += (baseSlot + historySlot) * handleStep;
            if (historyRes) {
                device->CreateShaderResourceView(historyRes, &srvDesc, histDest);
            } else if (m_dummySrvHeap) {
                device->CopyDescriptorsSimple(1, histDest, m_dummySrvHeap->GetCPUDescriptorHandleForHeapStart(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
            }
        }

        for (int i = kPostFxHistoryCount + 1; i < 8; ++i) {
            D3D12_CPU_DESCRIPTOR_HANDLE dummyDest = startHandle;
            dummyDest.ptr += (baseSlot + i) * handleStep;
            if (m_dummySrvHeap) {
                device->CopyDescriptorsSimple(1, dummyDest, m_dummySrvHeap->GetCPUDescriptorHandleForHeapStart(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
            }
        }
    };

    ID3D12Resource* currentInput = inputTexture;
    ID3D12Resource* currentOutput = ping;

    int passIndex = 0;
    for (auto& fx : chain) {
        if (!fx.enabled) continue;
        if (!fx.pipelineState) continue;
        if (passIndex >= kMaxPostFxChain) break;

        EnsurePostFxHistory(fx, width, height);
        if (fx.historyTextures.empty()) continue;

        if (!fx.historyInitialized) {
            for (int i = 0; i < kPostFxHistoryCount; ++i) {
                D3D12_RESOURCE_BARRIER initBarriers[2] = {};
                initBarriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                initBarriers[0].Transition.pResource = currentInput;
                initBarriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
                initBarriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
                initBarriers[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

                initBarriers[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                initBarriers[1].Transition.pResource = fx.historyTextures[i].Get();
                initBarriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
                initBarriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
                initBarriers[1].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
                commandList->ResourceBarrier(2, initBarriers);

                commandList->CopyResource(fx.historyTextures[i].Get(), currentInput);

                std::swap(initBarriers[0].Transition.StateBefore, initBarriers[0].Transition.StateAfter);
                std::swap(initBarriers[1].Transition.StateBefore, initBarriers[1].Transition.StateAfter);
                commandList->ResourceBarrier(2, initBarriers);
            }
            fx.historyInitialized = true;
            fx.historyIndex = 0;
        }

        D3D12_RESOURCE_BARRIER barrier = {};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = currentOutput;
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        commandList->ResourceBarrier(1, &barrier);

        int baseSlot = passIndex * 8;
        bindInput(currentInput, fx, baseSlot);
        ID3D12DescriptorHeap* heaps[] = { srvHeap };
        commandList->SetDescriptorHeaps(1, heaps);

        D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = rtvHeap->GetCPUDescriptorHandleForHeapStart();
        m_deviceRef->GetDevice()->CreateRenderTargetView(currentOutput, nullptr, rtvHandle);

        D3D12_GPU_DESCRIPTOR_HANDLE srvGpu = srvHeap->GetGPUDescriptorHandleForHeapStart();
        srvGpu.ptr += baseSlot * handleStep;
        float iBeat = 0.0f;
        float iBar = 0.0f;
        float fBeat = 0.0f;
        float fBarBeat = 0.0f;
        float fBarBeat16 = 0.0f;
        ComputeShaderMusicalTimingPostFx(m_transport, iBeat, iBar, fBeat, fBarBeat, fBarBeat16);
        m_previewRenderer->Render(
            commandList,
            fx.pipelineState.Get(),
            currentOutput,
            rtvHandle,
            srvGpu,
            width,
            height,
            static_cast<float>(timeSeconds),
            iBeat,
            iBar,
            fBarBeat16,
            fBeat,
            fBarBeat);

        std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
        commandList->ResourceBarrier(1, &barrier);

        int writeIndex = (fx.historyIndex + 1) % kPostFxHistoryCount;
        D3D12_RESOURCE_BARRIER historyBarriers[2] = {};
        historyBarriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        historyBarriers[0].Transition.pResource = currentOutput;
        historyBarriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        historyBarriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
        historyBarriers[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

        historyBarriers[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        historyBarriers[1].Transition.pResource = fx.historyTextures[writeIndex].Get();
        historyBarriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        historyBarriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
        historyBarriers[1].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        commandList->ResourceBarrier(2, historyBarriers);

        commandList->CopyResource(fx.historyTextures[writeIndex].Get(), currentOutput);

        std::swap(historyBarriers[0].Transition.StateBefore, historyBarriers[0].Transition.StateAfter);
        std::swap(historyBarriers[1].Transition.StateBefore, historyBarriers[1].Transition.StateAfter);
        commandList->ResourceBarrier(2, historyBarriers);
        fx.historyIndex = writeIndex;

        currentInput = currentOutput;
        currentOutput = (currentOutput == ping) ? pong : ping;
        passIndex++;
    }

    if (!usePreviewResources) {
        scene.postFxValid = true;
    }
    return currentInput;
}

} // namespace ShaderLab
