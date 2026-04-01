#pragma once

#include "ShaderLab/Core/ShaderLabData.h"
#include <d3d12.h>
#include <wrl/client.h>
#include <vector>
#include <string>
#include <array>
#include <unordered_map>

using Microsoft::WRL::ComPtr;

namespace ShaderLab {

class Device;
class Swapchain;
class PreviewRenderer;
class AudioSystem;
class ShaderCompiler;

class DemoPlayer {
public:
    DemoPlayer();
    ~DemoPlayer();

    bool Initialize(HWND hwnd, Device* device, Swapchain* swapchain, int width, int height);
    void LoadProject(const std::string& manifestPath);
    void SetLooping(bool enabled) { m_loopPlayback = enabled; }
    bool IsLooping() const { return m_loopPlayback; }
    void SetVsyncEnabled(bool enabled) { m_vsyncEnabled = enabled; }
    bool IsVsyncEnabled() const { return m_vsyncEnabled; }
    
    void Update(double wallTime, float dt);
    void Render(ID3D12GraphicsCommandList* commandList, ID3D12Resource* renderTarget, D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle);
    void OnResize(int width, int height);
    bool ConsumePendingRenderResolution(uint32_t& outWidth, uint32_t& outHeight);
    void Shutdown();

private:
    void SetActiveScene(int index);
    bool CompileScene(int sceneIndex);
    void EnsureSceneTexture(int sceneIndex);
    void RenderScene(ID3D12GraphicsCommandList* commandList, int sceneIndex, double time);
    std::string GetTransitionShader(const std::string& transitionPresetStem);
    void EnsurePostFxResources(Scene& scene);
    void EnsurePostFxHistory(Scene::PostFXEffect& effect);
    bool CompilePostFxEffect(Scene::PostFXEffect& effect, int sceneIndex, int fxIndex);
    bool CompileComputeEffect(Scene::ComputeEffect& effect, int sceneIndex, int computeIndex);
    ID3D12Resource* ApplyPostFxChain(ID3D12GraphicsCommandList* commandList,
                                    Scene& scene,
                                    ID3D12Resource* inputTexture,
                                    double timeSeconds);
    void EnsureComputeHistory(Scene::ComputeEffect& effect);
    ID3D12Resource* ApplyComputeChain(ID3D12GraphicsCommandList* commandList,
                                     int sceneIndex,
                                     std::vector<Scene::ComputeEffect>& chain,
                                     ID3D12Resource* inputTexture,
                                     double timeSeconds);
    ID3D12Resource* GetSceneFinalTexture(ID3D12GraphicsCommandList* commandList,
                                        int sceneIndex,
                                        double timeSeconds);
    bool EnsureTransitionPipeline(const std::string& transitionPresetStem);
    void PrimeRuntimeResources();
    
    // Core Refs
    Device* m_device = nullptr;
    Swapchain* m_swapchain = nullptr;
    PreviewRenderer* m_renderer = nullptr;
    bool m_rendererReady = false;
    AudioSystem* m_audio = nullptr;
    ShaderCompiler* m_compiler = nullptr; 
    bool m_compilerReady = false;

    // Data
    ProjectData m_project;
    
    // Loading State
    enum class LoadingStage {
        Idle,
        LoadingManifest,
        LoadingAssets,
        CompilingShaders,
        Ready
    };
    LoadingStage m_loadingStage = LoadingStage::Idle;
    std::string m_loadingStatus;
    bool m_loadingFailed = false;
    int m_compilationIndex = 0;
    std::string m_manifestPath;

    // Runtime State
    int m_activeSceneIndex = -1;
    float m_activeSceneOffset = 0.0f; // Offset in beats relative to scene start
    double m_activeSceneStartBeat = 0.0;
    Transport m_transport; // Runtime transport state
    
    // Render Resources
    uint32_t m_width = 0;
    uint32_t m_height = 0;
    
    ComPtr<ID3D12Resource> m_dummyTexture;
    ComPtr<ID3D12DescriptorHeap> m_dummySrvHeap;
    ComPtr<ID3D12DescriptorHeap> m_dummyRtvHeap;
    bool m_dummyTextureInitialized = false;
    ComPtr<ID3D12DescriptorHeap> m_imguiSrvHeap; // Dedicated heap for ImGui
    
    // Debug State
    bool m_showDebug = false;
    bool m_altPressed = false;
    double m_lastFrameTime = 0.0;
    int m_debugLastBeatLogged = -1;
    double m_debugLastShaderParamLogTime = -1000.0;
    TransportState m_debugLastTransportState = TransportState::Stopped;
    LoadingStage m_debugLastLoadingStage = LoadingStage::Idle;
    
    // Transition State
    bool m_transitionActive = false;
    double m_transitionStartBeat = 0.0;
    double m_transitionDurationBeats = 1.0;
    int m_transitionFromIndex = -1;
    int m_transitionToIndex = -1;
    double m_transitionFromStartBeat = 0.0;
    double m_transitionToStartBeat = 0.0;
    float m_transitionFromOffset = 0.0f;
    float m_transitionToOffset = 0.0f;
    std::string m_currentTransitionStem;
    int m_pendingActiveScene = -2;
    int m_transitionJustCompletedBeat = -1;
    
    ComPtr<ID3D12PipelineState> m_transitionPSO;
    ComPtr<ID3D12DescriptorHeap> m_transitionSrvHeap;
    std::string m_compiledTransitionStem;
    std::unordered_map<std::string, ComPtr<ID3D12PipelineState>> m_transitionPsoCache;

    std::vector<int> m_renderStack;
    std::vector<uint8_t> m_precompiledVertexShader;
    std::unordered_map<std::string, std::vector<uint8_t>> m_transitionBytecode;
    std::vector<std::vector<uint8_t>> m_microModuleBytecode;
    std::vector<int16_t> m_microSceneModuleIds;
    std::vector<std::vector<int16_t>> m_microPostFxModuleIds;
    std::array<int16_t, 6> m_microTransitionModuleIds = { -1, -1, -1, -1, -1, -1 };
    bool m_loopPlayback = true;
    bool m_vsyncEnabled = true;
    bool m_pendingRenderResolutionUpdate = false;
    uint32_t m_pendingRenderWidth = 0;
    uint32_t m_pendingRenderHeight = 0;
};

}
