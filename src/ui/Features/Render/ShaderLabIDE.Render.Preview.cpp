#include "ShaderLab/UI/ShaderLabIDE.h"
#include "ShaderLab/UI/UISystemAssets.h"
#include "ShaderLab/UI/UISystemDemoUtils.h"

#include "ShaderLab/Graphics/Device.h"
#include "ShaderLab/Graphics/PreviewRenderer.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace ShaderLab {

namespace {

void ComputeShaderMusicalTimingPreview(const PreviewTransport& transport,
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

bool ShaderLabIDE::RenderPreviewTexture(ID3D12GraphicsCommandList* commandList) {
    bool hasActiveScene = (m_activeSceneIndex >= 0 && m_activeSceneIndex < static_cast<int>(m_scenes.size()));

    if (!m_previewRenderer || !m_previewTexture) {
        return false;
    }

    m_renderStack.clear();

    if (m_currentMode == UIMode::PostFX) {
        int sourceIndex = m_postFxSourceSceneIndex;
        if (sourceIndex < 0 || sourceIndex >= static_cast<int>(m_scenes.size())) {
            D3D12_RESOURCE_BARRIER barrier = {};
            barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier.Transition.pResource = m_previewTexture.Get();
            barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
            barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
            barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            commandList->ResourceBarrier(1, &barrier);

            float clearColor[] = { 0.0f, 0.0f, 0.0f, 1.0f };
            commandList->ClearRenderTargetView(m_previewRtvHandle, clearColor, 0, nullptr);

            std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
            commandList->ResourceBarrier(1, &barrier);
            return true;
        }

        RenderScene(commandList, sourceIndex, m_previewTextureWidth, m_previewTextureHeight, m_transport.timeSeconds);
        ID3D12Resource* input = m_scenes[sourceIndex].texture.Get();
        if (!input) return false;

        ID3D12Resource* finalTex = input;
        if (!m_postFxDraftChain.empty()) {
            finalTex = ApplyPostFxChain(commandList,
                                        m_scenes[sourceIndex],
                                        m_postFxDraftChain,
                                        input,
                                        m_previewTextureWidth,
                                        m_previewTextureHeight,
                                        m_transport.timeSeconds,
                                        true);
        }

        if (!m_computeEffectDraftChain.empty()) {
            finalTex = ApplyComputeEffectChain(commandList,
                                               sourceIndex,
                                               m_computeEffectDraftChain,
                                               finalTex,
                                               m_previewTextureWidth,
                                               m_previewTextureHeight,
                                               m_transport.timeSeconds);
        }

        D3D12_RESOURCE_BARRIER preCopyBarriers[2] = {};
        preCopyBarriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        preCopyBarriers[0].Transition.pResource = m_previewTexture.Get();
        preCopyBarriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        preCopyBarriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
        preCopyBarriers[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

        preCopyBarriers[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        preCopyBarriers[1].Transition.pResource = finalTex;
        preCopyBarriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        preCopyBarriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
        preCopyBarriers[1].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

        commandList->ResourceBarrier(2, preCopyBarriers);
        commandList->CopyResource(m_previewTexture.Get(), finalTex);

        D3D12_RESOURCE_BARRIER postCopyBarriers[2] = {};
        postCopyBarriers[0] = preCopyBarriers[0];
        postCopyBarriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
        postCopyBarriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;

        postCopyBarriers[1] = preCopyBarriers[1];
        postCopyBarriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
        postCopyBarriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;

        commandList->ResourceBarrier(2, postCopyBarriers);
        return true;
    }

    if (m_transitionActive && m_currentMode != UIMode::Scene) {
        float beatsPerSec = m_transport.bpm / 60.0f;
        double exactBeat = m_transport.timeSeconds * beatsPerSec;
        double progress = (exactBeat - m_transitionStartBeat) / m_transitionDurationBeats;

        const bool isPlaying = (m_transport.state == TransportState::Playing);
        if (isPlaying && progress >= 1.0) {
            m_transitionActive = false;
            if (m_pendingActiveScene != -2) {
                SetActiveScene(m_pendingActiveScene);
                m_activeSceneStartBeat = m_transitionToStartBeat;
                m_activeSceneOffset = (m_pendingActiveScene >= 0) ? m_transitionToOffset : 0.0f;
                m_pendingActiveScene = -2;
            }
        } else {
            if (progress < 0.0) progress = 0.0;
            if (progress > 1.0) progress = 1.0;

            const std::string effectiveStem = m_currentTransitionStem;

            if (!m_transitionPSO || m_compiledTransitionStem != effectiveStem) {
                std::vector<PreviewRenderer::TextureDecl> decls = {
                    {0, "Texture2D"}, {1, "Texture2D"}
                };
                std::string code = GetEditorTransitionShaderSourceByStem(effectiveStem);
                std::vector<std::string> errs;
                m_transitionPSO = m_previewRenderer->CompileShader(code, decls, errs);
                m_compiledTransitionStem = effectiveStem;
            }

            bool validIndices = true;

            if (m_transitionFromIndex < 0 || m_transitionFromIndex >= static_cast<int>(m_scenes.size())) m_transitionFromIndex = -1;
            if (m_transitionToIndex < 0 || m_transitionToIndex >= static_cast<int>(m_scenes.size())) m_transitionToIndex = -1;

            if (m_transitionPSO && validIndices) {
                ID3D12Resource* fromTex = nullptr;
                ID3D12Resource* toTex = nullptr;
                const double fromTime = SceneTimeSeconds(exactBeat, m_transitionFromStartBeat, m_transitionFromOffset, m_transport.bpm);
                const double toTime = SceneTimeSeconds(exactBeat, m_transitionToStartBeat, m_transitionToOffset, m_transport.bpm);
                if (m_transitionFromIndex != -1) {
                    fromTex = GetSceneFinalTexture(commandList,
                                                   m_transitionFromIndex,
                                                   m_previewTextureWidth,
                                                   m_previewTextureHeight,
                                                   fromTime);
                }
                if (m_transitionToIndex != -1) {
                    toTex = GetSceneFinalTexture(commandList,
                                                 m_transitionToIndex,
                                                 m_previewTextureWidth,
                                                 m_previewTextureHeight,
                                                 toTime);
                }

                if (!fromTex) fromTex = m_dummyTexture.Get();
                if (!toTex) toTex = m_dummyTexture.Get();

                if (!m_transitionSrvHeap) {
                    D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
                    heapDesc.NumDescriptors = 8;
                    heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
                    heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
                    m_deviceRef->GetDevice()->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&m_transitionSrvHeap));
                }

                auto device = m_deviceRef->GetDevice();
                auto handleStep = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
                auto startCpu = m_transitionSrvHeap->GetCPUDescriptorHandleForHeapStart();

                auto Bind = [&](ID3D12Resource* res, int slot) {
                    D3D12_CPU_DESCRIPTOR_HANDLE dest = startCpu;
                    dest.ptr += slot * handleStep;
                    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
                    srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
                    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
                    srvDesc.Texture2D.MipLevels = 1;
                    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                    device->CreateShaderResourceView(res, &srvDesc, dest);
                };

                Bind(fromTex, 0);
                Bind(toTex, 1);

                D3D12_RESOURCE_BARRIER barrier = {};
                barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                barrier.Transition.pResource = m_previewTexture.Get();
                barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
                barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
                barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
                commandList->ResourceBarrier(1, &barrier);

                ID3D12DescriptorHeap* heaps[] = { m_transitionSrvHeap.Get() };
                commandList->SetDescriptorHeaps(1, heaps);

                float iBeat = 0.0f;
                float iBar = 0.0f;
                float fBeat = 0.0f;
                float fBarBeat = 0.0f;
                float fBarBeat16 = 0.0f;
                ComputeShaderMusicalTimingPreview(m_transport, iBeat, iBar, fBeat, fBarBeat, fBarBeat16);
                m_previewRenderer->Render(commandList,
                                          m_transitionPSO.Get(),
                                          m_previewTexture.Get(),
                                          m_previewRtvHandle,
                                          m_transitionSrvHeap->GetGPUDescriptorHandleForHeapStart(),
                                          m_previewTextureWidth,
                                          m_previewTextureHeight,
                                          static_cast<float>(progress),
                                          iBeat,
                                          iBar,
                                          fBarBeat16,
                                          fBeat,
                                          fBarBeat);

                std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
                commandList->ResourceBarrier(1, &barrier);

                return true;
            }
        }
    }

    hasActiveScene = (m_activeSceneIndex >= 0 && m_activeSceneIndex < static_cast<int>(m_scenes.size()));

    if (!hasActiveScene) {
        D3D12_RESOURCE_BARRIER barrier = {};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = m_previewTexture.Get();
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        commandList->ResourceBarrier(1, &barrier);

        float clearColor[] = { 0.0f, 0.0f, 0.0f, 1.0f };
        commandList->ClearRenderTargetView(m_previewRtvHandle, clearColor, 0, nullptr);

        std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
        commandList->ResourceBarrier(1, &barrier);
        return true;
    }

    const double beatsPerSec = m_transport.bpm / 60.0f;
    const double exactBeat = m_transport.timeSeconds * beatsPerSec;
    const double activeTime = SceneTimeSeconds(exactBeat, m_activeSceneStartBeat, m_activeSceneOffset, m_transport.bpm);
    ID3D12Resource* finalTex = GetSceneFinalTexture(commandList,
                                                    m_activeSceneIndex,
                                                    m_previewTextureWidth,
                                                    m_previewTextureHeight,
                                                    activeTime);

    if (finalTex) {
        D3D12_RESOURCE_BARRIER preCopyBarriers[2] = {};
        preCopyBarriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        preCopyBarriers[0].Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
        preCopyBarriers[0].Transition.pResource = m_previewTexture.Get();
        preCopyBarriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        preCopyBarriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
        preCopyBarriers[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

        preCopyBarriers[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        preCopyBarriers[1].Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
        preCopyBarriers[1].Transition.pResource = finalTex;
        preCopyBarriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        preCopyBarriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
        preCopyBarriers[1].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

        commandList->ResourceBarrier(2, preCopyBarriers);

        commandList->CopyResource(m_previewTexture.Get(), finalTex);

        D3D12_RESOURCE_BARRIER postCopyBarriers[2] = {};
        postCopyBarriers[0] = preCopyBarriers[0];
        postCopyBarriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
        postCopyBarriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;

        postCopyBarriers[1] = preCopyBarriers[1];
        postCopyBarriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
        postCopyBarriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;

        commandList->ResourceBarrier(2, postCopyBarriers);
    }

    return true;
}

void ShaderLabIDE::QueuePreviewVideoCapture(ID3D12GraphicsCommandList* commandList) {
    if (!commandList || !m_deviceRef || !m_previewTexture || m_previewTextureWidth == 0 || m_previewTextureHeight == 0) {
        return;
    }

    auto* device = m_deviceRef->GetDevice();
    D3D12_RESOURCE_DESC srcDesc = m_previewTexture->GetDesc();

    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint = {};
    UINT numRows = 0;
    UINT64 rowSizeInBytes = 0;
    UINT64 totalBytes = 0;
    device->GetCopyableFootprints(&srcDesc, 0, 1, 0, &footprint, &numRows, &rowSizeInBytes, &totalBytes);

    if (!m_previewVideoExportReadbackBuffer || m_previewVideoExportReadbackBytes != totalBytes ||
        m_previewVideoExportReadbackRowPitch != footprint.Footprint.RowPitch) {
        m_previewVideoExportReadbackBuffer.Reset();

        D3D12_HEAP_PROPERTIES heapProps = {};
        heapProps.Type = D3D12_HEAP_TYPE_READBACK;

        D3D12_RESOURCE_DESC bufferDesc = {};
        bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        bufferDesc.Width = totalBytes;
        bufferDesc.Height = 1;
        bufferDesc.DepthOrArraySize = 1;
        bufferDesc.MipLevels = 1;
        bufferDesc.SampleDesc.Count = 1;
        bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

        if (FAILED(device->CreateCommittedResource(&heapProps,
                                                   D3D12_HEAP_FLAG_NONE,
                                                   &bufferDesc,
                                                   D3D12_RESOURCE_STATE_COPY_DEST,
                                                   nullptr,
                                                   IID_PPV_ARGS(m_previewVideoExportReadbackBuffer.ReleaseAndGetAddressOf())))) {
            m_previewVideoExportStatus = "Failed to allocate export readback buffer.";
            CancelPreviewVideoExport(true);
            return;
        }

        m_previewVideoExportReadbackBytes = totalBytes;
        m_previewVideoExportReadbackRowPitch = footprint.Footprint.RowPitch;
    }

    D3D12_RESOURCE_BARRIER preBarrier = {};
    preBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    preBarrier.Transition.pResource = m_previewTexture.Get();
    preBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    preBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
    preBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    commandList->ResourceBarrier(1, &preBarrier);

    D3D12_TEXTURE_COPY_LOCATION srcLoc = {};
    srcLoc.pResource = m_previewTexture.Get();
    srcLoc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    srcLoc.SubresourceIndex = 0;

    D3D12_TEXTURE_COPY_LOCATION dstLoc = {};
    dstLoc.pResource = m_previewVideoExportReadbackBuffer.Get();
    dstLoc.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    dstLoc.PlacedFootprint = footprint;

    commandList->CopyTextureRegion(&dstLoc, 0, 0, 0, &srcLoc, nullptr);

    D3D12_RESOURCE_BARRIER postBarrier = preBarrier;
    postBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
    postBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    commandList->ResourceBarrier(1, &postBarrier);

    m_previewVideoExportPendingReadback = true;
    m_previewVideoExportPendingFrameIndex = static_cast<int64_t>(m_previewVideoExportCapturedFrames);
}

} // namespace ShaderLab
