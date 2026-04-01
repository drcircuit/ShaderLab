#include "ShaderLab/App/DemoPlayer.h"

#include <algorithm>
#include <cmath>

#include "ShaderLab/Graphics/Device.h"
#include "ShaderLab/Graphics/PreviewRenderer.h"

namespace ShaderLab {

namespace {

constexpr int kPostFxHistoryCountEffectChains = 4;
constexpr int kMaxPostFxChainEffectChains = 32;

void ComputeShaderMusicalTimingEffectChains(const Transport& transport,
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

ID3D12Resource* DemoPlayer::ApplyPostFxChain(ID3D12GraphicsCommandList* commandList,
                                            Scene& scene,
                                            ID3D12Resource* inputTexture,
                                            double timeSeconds) {
    if (!commandList || !inputTexture) return inputTexture;

    bool anyEnabled = false;
    for (const auto& fx : scene.postFxChain) {
        if (fx.enabled) { anyEnabled = true; break; }
    }
    if (!anyEnabled) return inputTexture;

    EnsurePostFxResources(scene);
    if (!scene.postFxTextureA || !scene.postFxTextureB || !scene.postFxSrvHeap || !scene.postFxRtvHeap) return inputTexture;

    auto device = m_device->GetDevice();
    auto handleStep = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    auto startHandle = scene.postFxSrvHeap->GetCPUDescriptorHandleForHeapStart();

    auto bindInput = [&](ID3D12Resource* src, Scene::PostFXEffect& fx, int baseSlot) {
        D3D12_CPU_DESCRIPTOR_HANDLE dest = startHandle;
        dest.ptr += baseSlot * handleStep;
        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MipLevels = 1;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        device->CreateShaderResourceView(src, &srvDesc, dest);

        for (int i = 1; i <= kPostFxHistoryCountEffectChains; ++i) {
            int historySlot = i;
            int historyIndex = fx.historyIndex - (i - 1);
            while (historyIndex < 0) historyIndex += kPostFxHistoryCountEffectChains;
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

        for (int i = kPostFxHistoryCountEffectChains + 1; i < 8; ++i) {
            D3D12_CPU_DESCRIPTOR_HANDLE dummyDest = startHandle;
            dummyDest.ptr += (baseSlot + i) * handleStep;
            if (m_dummySrvHeap) {
                device->CopyDescriptorsSimple(1, dummyDest, m_dummySrvHeap->GetCPUDescriptorHandleForHeapStart(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
            }
        }
    };

    ID3D12Resource* ping = scene.postFxTextureA.Get();
    ID3D12Resource* pong = scene.postFxTextureB.Get();
    ID3D12Resource* currentInput = inputTexture;
    ID3D12Resource* currentOutput = ping;

    int passIndex = 0;
    for (auto& fx : scene.postFxChain) {
        if (!fx.enabled) continue;
        if (!fx.pipelineState) continue;
        if (passIndex >= kMaxPostFxChainEffectChains) break;

        EnsurePostFxHistory(fx);
        if (fx.historyTextures.empty()) continue;

        if (!fx.historyInitialized) {
            for (int i = 0; i < kPostFxHistoryCountEffectChains; ++i) {
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
        ID3D12DescriptorHeap* heaps[] = { scene.postFxSrvHeap.Get() };
        commandList->SetDescriptorHeaps(1, heaps);

        D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = scene.postFxRtvHeap->GetCPUDescriptorHandleForHeapStart();
        m_device->GetDevice()->CreateRenderTargetView(currentOutput, nullptr, rtvHandle);

        D3D12_GPU_DESCRIPTOR_HANDLE srvGpu = scene.postFxSrvHeap->GetGPUDescriptorHandleForHeapStart();
        srvGpu.ptr += baseSlot * handleStep;
        float iBeat = 0.0f;
        float iBar = 0.0f;
        float fBeat = 0.0f;
        float fBarBeat = 0.0f;
        float fBarBeat16 = 0.0f;
        ComputeShaderMusicalTimingEffectChains(m_transport, iBeat, iBar, fBeat, fBarBeat, fBarBeat16);
        m_renderer->Render(
            commandList,
            fx.pipelineState.Get(),
            currentOutput,
            rtvHandle,
            srvGpu,
            m_width, m_height,
            (float)timeSeconds,
            iBeat,
            iBar,
            fBarBeat16,
            fBeat,
            fBarBeat
        );

        std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
        commandList->ResourceBarrier(1, &barrier);

        int writeIndex = (fx.historyIndex + 1) % kPostFxHistoryCountEffectChains;
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

    scene.postFxValid = true;
    return currentInput;
}

}