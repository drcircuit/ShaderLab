#pragma once
/*
UNIFIED EFFECT CHAIN PROCESSOR

This system handles both:
  - PostFXEffect (pixel shader based, buffer ping-pong)
  - ComputeEffect (compute shader based, optimized algorithms)

The key architectural difference:
  PostFX: Render fullscreen quad with pixel shader
  Compute: Dispatch compute shader with UAV output

But from the user's perspective, they're interchangeable in the effect chain.
A scene can have:
  1. PostFX Effect A
  2. Compute Effect B
  3. PostFX Effect C
  4. Compute Effect D
  ...and they chain together seamlessly
*/

#include "ShaderLab/Core/ShaderLabData.h"
#include <memory>
#include <variant>
#include <vector>
#include <d3d12.h>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

namespace ShaderLab {

// Forward declarations
class Device;

// Effect dispatch type
enum class EffectKind {
    PostFX,
    Compute
};

// Unified effect handle (can point to either type)
struct EffectHandle {
    EffectKind kind;
    size_t index;  // Index in postFxChain or computeEffectChain
};

// ============================================================================
// UNIFIED EFFECT CHAIN PROCESSOR
// ============================================================================

class EffectChainProcessor {
public:
    EffectChainProcessor();
    ~EffectChainProcessor();

    bool Initialize(Device* device);
    void Shutdown();

    // Apply a unified effect chain to a texture
    // Intelligently dispatches to PostFX or Compute handlers
    ID3D12Resource* ApplyEffectChain(
        ID3D12GraphicsCommandList* commandList,
        Scene& scene,
        ID3D12Resource* inputTexture,
        uint32_t width,
        uint32_t height,
        double timeSeconds);

    // Compile either type of effect
    bool CompilePostFXEffect(Scene::PostFXEffect& effect, std::vector<std::string>& outErrors);
    bool CompileComputeEffect(Scene::ComputeEffect& effect, std::vector<std::string>& outErrors);

    // Resource setup
    void EnsurePostFxResources(Scene& scene, uint32_t width, uint32_t height);
    void EnsureComputeEffectResources(Scene::ComputeEffect& effect, uint32_t width, uint32_t height);
    void EnsurePostFxHistory(Scene::PostFXEffect& effect, uint32_t width, uint32_t height);
    void EnsureComputeEffectHistory(Scene::ComputeEffect& effect, uint32_t width, uint32_t height);

private:
    // Internal dispatch functions
    ID3D12Resource* ApplyPostFXEffect(
        ID3D12GraphicsCommandList* commandList,
        Scene& scene,
        Scene::PostFXEffect& effect,
        ID3D12Resource* inputTexture,
        uint32_t width,
        uint32_t height,
        double timeSeconds);

    ID3D12Resource* ApplyComputeEffect(
        ID3D12GraphicsCommandList* commandList,
        Scene::ComputeEffect& effect,
        ID3D12Resource* inputTexture,
        ID3D12Resource*& outputTexture,
        uint32_t width,
        uint32_t height,
        double timeSeconds);

    Device* m_device = nullptr;
    
    // Reusable resources for compute effects
    ComPtr<ID3D12RootSignature> m_computeRootSignature;
    ComPtr<ID3D12Resource> m_computeParamsBuffer;  // Constant buffer for compute effects
    ComPtr<ID3D12DescriptorHeap> m_computeDescriptorHeap;

    bool CreateComputeRootSignature();
    bool UpdateComputeParamsBuffer(uint32_t width, uint32_t height, double time);
};

} // namespace ShaderLab
