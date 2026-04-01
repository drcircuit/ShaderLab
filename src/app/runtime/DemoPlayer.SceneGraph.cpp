#include "ShaderLab/App/DemoPlayer.h"

#include <algorithm>
#include <cmath>

#include "ShaderLab/Graphics/Device.h"
#include "ShaderLab/Graphics/PreviewRenderer.h"

namespace ShaderLab {

namespace {

void ComputeShaderMusicalTimingSceneGraph(const Transport& transport,
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

ID3D12Resource* DemoPlayer::GetSceneFinalTexture(ID3D12GraphicsCommandList* commandList,
                                                 int sceneIndex,
                                                 double timeSeconds) {
    if (sceneIndex < 0 || sceneIndex >= (int)m_project.scenes.size()) return nullptr;
    RenderScene(commandList, sceneIndex, timeSeconds);
    auto& scene = m_project.scenes[sceneIndex];
    if (!scene.texture) return nullptr;
    ID3D12Resource* output = scene.texture.Get();
    if (!scene.postFxChain.empty()) {
        output = ApplyPostFxChain(commandList, scene, output, timeSeconds);
    }
#if !SHADERLAB_TINY_PLAYER
    if (!scene.computeEffectChain.empty()) {
        output = ApplyComputeChain(commandList, sceneIndex, scene.computeEffectChain, output, timeSeconds);
    }
#endif
    return output;
}

void DemoPlayer::OnResize(int width, int height) {
    m_width = width;
    m_height = height;
}

void DemoPlayer::RenderScene(ID3D12GraphicsCommandList* cmd, int sceneIndex, double time) {
    for (int s : m_renderStack) {
        if (s == sceneIndex) return;
    }
    m_renderStack.push_back(sceneIndex);

    EnsureSceneTexture(sceneIndex);
    auto& scene = m_project.scenes[sceneIndex];

    if (!scene.texture) {
        m_renderStack.pop_back();
        return;
    }

    for (const auto& binding : scene.bindings) {
        if (binding.enabled && binding.sourceSceneIndex != -1 && binding.sourceSceneIndex != sceneIndex) {
            RenderScene(cmd, binding.sourceSceneIndex, time);
        }
    }

    if (!scene.pipelineState) {
        m_renderStack.pop_back();
        return;
    }

    if (scene.srvHeap) {
        auto device = m_device->GetDevice();
        auto handleStep = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        auto startHandle = scene.srvHeap->GetCPUDescriptorHandleForHeapStart();

        for (int i = 0; i < 8; ++i) {
            D3D12_CPU_DESCRIPTOR_HANDLE dest = startHandle;
            dest.ptr += i * handleStep;

            bool bound = false;
            for (const auto& b : scene.bindings) {
                if (b.channelIndex == i && b.enabled) {
                    ID3D12Resource* srcRes = nullptr;
                    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
                    srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
                    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

                    if (b.bindingType == BindingType::Scene) {
                        if (b.sourceSceneIndex >= 0 && b.sourceSceneIndex < (int)m_project.scenes.size()) {
                            auto& src = m_project.scenes[b.sourceSceneIndex];
                            if (src.texture) {
                                srcRes = src.texture.Get();
                                if (b.type == TextureType::TextureCube) {
                                    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
                                    srvDesc.TextureCube.MipLevels = 1;
                                } else {
                                    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
                                    srvDesc.Texture2D.MipLevels = 1;
                                }
                            }
                        }
                    }

                    if (srcRes) {
                        device->CreateShaderResourceView(srcRes, &srvDesc, dest);
                        bound = true;
                    }
                }
            }

            if (!bound) {
                D3D12_SHADER_RESOURCE_VIEW_DESC nullSrvDesc = {};
                nullSrvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
                nullSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
                nullSrvDesc.Texture2D.MipLevels = 1;
                nullSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                device->CreateShaderResourceView(nullptr, &nullSrvDesc, dest);
            }
        }
    }

    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = scene.texture.Get();
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    cmd->ResourceBarrier(1, &barrier);

    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = scene.rtvHeap->GetCPUDescriptorHandleForHeapStart();

    float clearColor[] = { 0, 0, 0, 1 };
    cmd->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);
    cmd->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);

    ID3D12DescriptorHeap* heaps[] = { scene.srvHeap.Get() };
    if (scene.srvHeap) cmd->SetDescriptorHeaps(1, heaps);

    float iBeat = 0.0f;
    float iBar = 0.0f;
    float fBeat = 0.0f;
    float fBarBeat = 0.0f;
    float fBarBeat16 = 0.0f;
    ComputeShaderMusicalTimingSceneGraph(m_transport, iBeat, iBar, fBeat, fBarBeat, fBarBeat16);
    m_renderer->Render(cmd,
                       scene.pipelineState.Get(),
                       scene.texture.Get(),
                       rtvHandle,
                       scene.srvHeap ? scene.srvHeap->GetGPUDescriptorHandleForHeapStart() : D3D12_GPU_DESCRIPTOR_HANDLE{},
                       m_width,
                       m_height,
                       (float)time,
                       iBeat,
                       iBar,
                       fBarBeat16,
                       fBeat,
                       fBarBeat);

    std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
    cmd->ResourceBarrier(1, &barrier);

    scene.textureValid = true;
    m_renderStack.pop_back();
}

} // namespace ShaderLab