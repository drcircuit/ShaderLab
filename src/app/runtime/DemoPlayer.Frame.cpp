#include "ShaderLab/App/DemoPlayer.h"

#include <algorithm>
#include <cmath>
#include <cctype>

#include "ShaderLab/Core/PackageManager.h"
#include "ShaderLab/Graphics/Device.h"
#include "ShaderLab/Graphics/PreviewRenderer.h"

#ifndef SHADERLAB_RUNTIME_IMGUI
#define SHADERLAB_RUNTIME_IMGUI 1
#endif

#ifndef SHADERLAB_TINY_DEV_OVERLAY
#define SHADERLAB_TINY_DEV_OVERLAY 0
#endif

#if SHADERLAB_RUNTIME_IMGUI
#include "imgui.h"
#include "backends/imgui_impl_win32.h"
#include "backends/imgui_impl_dx12.h"
#endif

namespace ShaderLab {

namespace {

void ComputeShaderMusicalTimingFrame(const Transport& transport,
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

double BeatSecondsFrame(float bpm) {
    if (bpm <= 0.0f) return 0.0;
    return 60.0 / static_cast<double>(bpm);
}

double SceneTimeSecondsFrame(double exactBeat, double startBeat, float offsetBeats, float bpm) {
    const double beatSeconds = BeatSecondsFrame(bpm);
    if (beatSeconds <= 0.0) return 0.0;
    const double sceneBeats = exactBeat - startBeat + static_cast<double>(offsetBeats);
    return sceneBeats * beatSeconds;
}

std::string CanonicalTransitionStemFrame(const std::string& transitionPresetStem) {
    std::string stem = transitionPresetStem;
    std::transform(stem.begin(), stem.end(), stem.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return stem;
}

} // namespace

void DemoPlayer::Render(ID3D12GraphicsCommandList* cmd, ID3D12Resource* renderTarget, D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle) {
    if (renderTarget && (m_width <= 0 || m_height <= 0)) {
        const D3D12_RESOURCE_DESC rtDesc = renderTarget->GetDesc();
        if (rtDesc.Width > 0 && rtDesc.Height > 0) {
            m_width = static_cast<int>(rtDesc.Width);
            m_height = static_cast<int>(rtDesc.Height);
        }
    }

    auto copyTextureToBackbuffer = [&](ID3D12Resource* srcTexture) -> bool {
        if (!srcTexture || !renderTarget) {
            return false;
        }

        const auto srcDesc = srcTexture->GetDesc();
        const auto dstDesc = renderTarget->GetDesc();
        if (srcDesc.Width != dstDesc.Width || srcDesc.Height != dstDesc.Height || srcDesc.Format != dstDesc.Format) {
            return false;
        }

        D3D12_RESOURCE_BARRIER dstBarrier = {};
        dstBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        dstBarrier.Transition.pResource = renderTarget;
        dstBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        dstBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
        cmd->ResourceBarrier(1, &dstBarrier);

        D3D12_RESOURCE_BARRIER srcBarrier = {};
        srcBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        srcBarrier.Transition.pResource = srcTexture;
        srcBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        srcBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
        cmd->ResourceBarrier(1, &srcBarrier);

        cmd->CopyResource(renderTarget, srcTexture);

        std::swap(dstBarrier.Transition.StateBefore, dstBarrier.Transition.StateAfter);
        cmd->ResourceBarrier(1, &dstBarrier);
        std::swap(srcBarrier.Transition.StateBefore, srcBarrier.Transition.StateAfter);
        cmd->ResourceBarrier(1, &srcBarrier);
        return true;
    };

    auto renderSceneDirectToBackbuffer = [&](int sceneIndex, double sceneTime) -> bool {
        if (sceneIndex < 0 || sceneIndex >= static_cast<int>(m_project.scenes.size())) {
            return false;
        }

        auto& scene = m_project.scenes[static_cast<size_t>(sceneIndex)];
        if (!scene.pipelineState || !scene.postFxChain.empty()) {
            return false;
        }

        for (const auto& b : scene.bindings) {
            if (b.enabled) return false;
        }

        float iBeat = 0.0f;
        float iBar = 0.0f;
        float fBeat = 0.0f;
        float fBarBeat = 0.0f;
        float fBarBeat16 = 0.0f;
        ComputeShaderMusicalTimingFrame(m_transport, iBeat, iBar, fBeat, fBarBeat, fBarBeat16);

        if (scene.srvHeap) {
            ID3D12DescriptorHeap* heaps[] = { scene.srvHeap.Get() };
            cmd->SetDescriptorHeaps(1, heaps);
        }

        m_renderer->Render(
            cmd,
            scene.pipelineState.Get(),
            renderTarget,
            rtvHandle,
            scene.srvHeap ? scene.srvHeap->GetGPUDescriptorHandleForHeapStart() : D3D12_GPU_DESCRIPTOR_HANDLE{},
            m_width,
            m_height,
            static_cast<float>(sceneTime),
            iBeat,
            iBar,
            fBarBeat16,
            fBeat,
            fBarBeat);
        return true;
    };

    if (!m_dummyTextureInitialized && m_dummyTexture && m_dummyRtvHeap) {
        const float black[4] = {0.0f, 0.0f, 0.0f, 1.0f};
        auto dummyRtv = m_dummyRtvHeap->GetCPUDescriptorHandleForHeapStart();
        cmd->ClearRenderTargetView(dummyRtv, black, 0, nullptr);

        D3D12_RESOURCE_BARRIER dummyBarrier = {};
        dummyBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        dummyBarrier.Transition.pResource = m_dummyTexture.Get();
        dummyBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        dummyBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        cmd->ResourceBarrier(1, &dummyBarrier);

        m_dummyTextureInitialized = true;
    }

#if SHADERLAB_RUNTIME_IMGUI
    auto loadingStageLabel = [&](LoadingStage stage) -> const char* {
        switch (stage) {
            case LoadingStage::Idle: return "Idle";
            case LoadingStage::LoadingManifest: return "LoadingManifest";
            case LoadingStage::LoadingAssets: return "LoadingAssets";
            case LoadingStage::CompilingShaders: return "CompilingShaders";
            case LoadingStage::Ready: return "Ready";
        }
        return "Unknown";
    };

    ImGui_ImplDX12_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

#if !SHADERLAB_TINY_PLAYER
    ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(static_cast<float>(m_width), 34.0f), ImGuiCond_Always);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 7.0f));
    if (ImGui::Begin("Runtime Titlebar", nullptr,
        ImGuiWindowFlags_NoDecoration |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoScrollWithMouse |
        ImGuiWindowFlags_NoNav)) {
        ImGui::Text("FPS: %.1f", ImGui::GetIO().Framerate);
        ImGui::SameLine();
        ImGui::Dummy(ImVec2(18.0f, 0.0f));
        ImGui::SameLine();
        bool vsyncEnabled = m_vsyncEnabled;
        if (ImGui::Checkbox("VSync", &vsyncEnabled)) {
            m_vsyncEnabled = vsyncEnabled;
        }
        ImGui::SameLine();
        ImGui::TextDisabled("(%s)", m_vsyncEnabled ? "Capped" : "Unlimited");
    }
    ImGui::End();
    ImGui::PopStyleVar(3);
#endif

    if (m_showDebug) {
        ImGui::SetNextWindowPos(ImVec2(10.0f, 10.0f), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowBgAlpha(0.5f);
        if (ImGui::Begin("Debug Overlay", &m_showDebug, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav)) {
            ImGui::Text("FPS: %.1f", ImGui::GetIO().Framerate);
            ImGui::Text("Time: %.2f s", m_transport.timeSeconds);
            if (m_loadingStage != LoadingStage::Ready) {
                ImGui::Separator();
                ImGui::TextColored(ImVec4(1,1,0,1), "Loading Stage: %d", (int)m_loadingStage);
            } else {
                ImGui::Text("Scene: %d", m_activeSceneIndex);
                if (m_transitionActive) ImGui::TextColored(ImVec4(0.4f,1.0f,0.4f,1.0f), "Transition Active");
            }
        }
        ImGui::End();
    }

#if SHADERLAB_TINY_DEV_OVERLAY && SHADERLAB_TINY_PLAYER
    ImGui::SetNextWindowPos(ImVec2(10.0f, 10.0f), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.65f);
    if (ImGui::Begin("Tiny Dev Console", nullptr,
        ImGuiWindowFlags_NoDecoration |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoFocusOnAppearing |
        ImGuiWindowFlags_NoNav)) {
        ImGui::Text("FPS: %.1f", ImGui::GetIO().Framerate);
        ImGui::SameLine();
        ImGui::Text("Time: %.2f s", m_transport.timeSeconds);
        ImGui::Text("Load: %d  Scene: %d  Transition: %s",
            static_cast<int>(m_loadingStage),
            m_activeSceneIndex,
            m_transitionActive ? "on" : "off");

        ImGui::Separator();
        ImGui::BeginChild("tiny_dev_log", ImVec2(640.0f, 220.0f), true, ImGuiWindowFlags_HorizontalScrollbar);
        const auto& lines = TinyDevLogLines();
        for (const std::string& line : lines) {
            ImGui::TextUnformatted(line.c_str());
        }
        if (!lines.empty()) {
            ImGui::SetScrollHereY(1.0f);
        }
        ImGui::EndChild();
    }
    ImGui::End();
#endif

#if SHADERLAB_TINY_PLAYER
    if (m_loadingStage != LoadingStage::Ready) {
        ImGui::SetNextWindowPos(ImVec2(10.0f, 10.0f), ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.75f);
        if (ImGui::Begin("Tiny Loading", nullptr,
            ImGuiWindowFlags_NoDecoration |
            ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("Stage: %s", loadingStageLabel(m_loadingStage));
            if (!m_loadingStatus.empty()) {
                ImGui::TextWrapped("%s", m_loadingStatus.c_str());
            }
            ImGui::Text("Packed: %s", PackageManager::Get().IsPacked() ? "yes" : "no");
        }
        ImGui::End();
    }
#endif
#endif

    if (m_loadingStage != LoadingStage::Ready) {
        float clearColor[] = {0.0f, 0.0f, 0.0f, 1.0f};
        if (m_loadingFailed) {
            clearColor[0] = 0.45f;
            clearColor[1] = 0.05f;
            clearColor[2] = 0.05f;
        }
        cmd->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);
        goto render_ui;
    }

    m_renderStack.clear();

    if (m_transitionActive) {
        float beatsPerSec = m_transport.bpm / 60.0f;
        double exactBeat = m_transport.timeSeconds * beatsPerSec;
        double progress = (exactBeat - m_transitionStartBeat) / m_transitionDurationBeats;

        if (progress >= 1.0) {
            m_transitionActive = false;
            if (m_pendingActiveScene != -2) {
                SetActiveScene(m_pendingActiveScene);
                m_activeSceneStartBeat = m_transitionToStartBeat;
                m_activeSceneOffset = (m_pendingActiveScene >= 0) ? m_transitionToOffset : 0.0f;
                m_pendingActiveScene = -2;
            }
        } else {
            ID3D12Resource* fromTex = nullptr;
            ID3D12Resource* toTex = nullptr;

            if (m_transitionFromIndex >= 0) {
                const double fromTime = SceneTimeSecondsFrame(exactBeat, m_transitionFromStartBeat, m_transitionFromOffset, m_transport.bpm);
                fromTex = GetSceneFinalTexture(cmd, m_transitionFromIndex, fromTime);
            }
            if (m_transitionToIndex >= 0) {
                const double toTime = SceneTimeSecondsFrame(exactBeat, m_transitionToStartBeat, m_transitionToOffset, m_transport.bpm);
                toTex = GetSceneFinalTexture(cmd, m_transitionToIndex, toTime);
            }

            const std::string canonicalTransitionStem = CanonicalTransitionStemFrame(m_currentTransitionStem);
            if (!m_transitionPSO || m_compiledTransitionStem != canonicalTransitionStem) {
                EnsureTransitionPipeline(canonicalTransitionStem);
            }
            if (m_transitionPSO) {
                if (!m_transitionSrvHeap) {
                    D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
                    heapDesc.NumDescriptors = 8;
                    heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
                    heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
                    m_device->GetDevice()->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&m_transitionSrvHeap));
                }
                auto device = m_device->GetDevice();
                auto handleStep = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
                auto start = m_transitionSrvHeap->GetCPUDescriptorHandleForHeapStart();

                auto Bind = [&](ID3D12Resource* res, int slot) {
                    D3D12_CPU_DESCRIPTOR_HANDLE dest = start;
                    dest.ptr += slot * handleStep;
                    if (!res) {
                        if (m_dummySrvHeap) {
                            device->CopyDescriptorsSimple(1, dest, m_dummySrvHeap->GetCPUDescriptorHandleForHeapStart(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
                        } else {
                            D3D12_SHADER_RESOURCE_VIEW_DESC nullSrv = {};
                            nullSrv.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
                            nullSrv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
                            nullSrv.Texture2D.MipLevels = 1;
                            nullSrv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                            device->CreateShaderResourceView(nullptr, &nullSrv, dest);
                        }
                        return;
                    }
                    D3D12_SHADER_RESOURCE_VIEW_DESC srv = {};
                    srv.Format = res->GetDesc().Format;
                    srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
                    srv.Texture2D.MipLevels = 1;
                    srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                    device->CreateShaderResourceView(res, &srv, dest);
                };
                Bind(fromTex, 0);
                Bind(toTex, 1);

                ID3D12DescriptorHeap* heaps[] = { m_transitionSrvHeap.Get() };
                cmd->SetDescriptorHeaps(1, heaps);

                float iBeat = 0.0f;
                float iBar = 0.0f;
                float fBeat = 0.0f;
                float fBarBeat = 0.0f;
                float fBarBeat16 = 0.0f;
                ComputeShaderMusicalTimingFrame(m_transport, iBeat, iBar, fBeat, fBarBeat, fBarBeat16);
                m_renderer->Render(cmd,
                                  m_transitionPSO.Get(),
                                  renderTarget,
                                  rtvHandle,
                                  m_transitionSrvHeap->GetGPUDescriptorHandleForHeapStart(),
                                  m_width,
                                  m_height,
                                  (float)progress,
                                  iBeat,
                                  iBar,
                                  fBarBeat16,
                                  fBeat,
                                  fBarBeat);
            } else {
                float clearColor[] = {0, 0, 0, 1};
                cmd->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);
            }
            goto render_ui;
        }
    }

    if (m_activeSceneIndex >= 0) {
        const double beatsPerSec = m_transport.bpm / 60.0f;
        const double exactBeat = m_transport.timeSeconds * beatsPerSec;
        const double activeTime = SceneTimeSecondsFrame(exactBeat, m_activeSceneStartBeat, m_activeSceneOffset, m_transport.bpm);

        if (renderSceneDirectToBackbuffer(m_activeSceneIndex, activeTime)) {
            goto render_ui;
        }

        ID3D12Resource* finalTex = GetSceneFinalTexture(cmd, m_activeSceneIndex, activeTime);
        if (finalTex) {
            if (!copyTextureToBackbuffer(finalTex)) {
                float clearColor[] = {0, 0, 0, 1};
                cmd->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);
            }
        }
    } else {
        float clearColor[] = {0,0,0,1};
        cmd->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);
    }

render_ui:
    (void)0;
#if SHADERLAB_RUNTIME_IMGUI
    ImGui::Render();
    if (m_imguiSrvHeap) {
        ID3D12DescriptorHeap* heaps[] = { m_imguiSrvHeap.Get() };
        cmd->SetDescriptorHeaps(1, heaps);
        ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), cmd);
    }
#endif
}

} // namespace ShaderLab