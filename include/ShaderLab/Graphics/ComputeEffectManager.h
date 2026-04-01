#pragma once
/*
GENERALIZED COMPUTE SHADER FRAMEWORK FOR SHADERLAB

This system allows effects to use compute shaders with standardized input/output
pipelines. Any effect that can be expressed as:
  - Input texture → Compute work → Output texture
...can be implemented as a compute shader effect.

EXAMPLES OF EFFECTS TO CREATE:
  - Temporal accumulation (motion trails, persistent glow)
  - Motion estimation (optical flow)
  - Denoising (temporal denoise)
  - Post-processing (tone mapping, filtering)
  - Image processing (convolution, morphology)
  - Custom effects (user-defined algorithms)
*/

#include <d3d12.h>
#include <wrl/client.h>
#include <string>
#include <vector>
#include <cstdint>

using Microsoft::WRL::ComPtr;

namespace ShaderLab {

// ============================================================================
// STANDARD PARAMETERS THAT ANY COMPUTE EFFECT CAN USE
// ============================================================================

struct ComputeEffectParameters {
    // Timing
    float iTime = 0.0f;
    float beat = 0.0f;
    float bar = 0.0f;
    float barBeat16 = 0.0f;

    // Resolution
    uint32_t width = 0;
    uint32_t height = 0;

    // Effect-specific parameters (user-defined, float4 basis)
    float param0 = 0.0f;   // Decay, radius, threshold, etc.
    float param1 = 0.0f;   // Blend, intensity, strength, etc.
    float param2 = 0.0f;   // Saturation, falloff, speed, etc.
    float param3 = 0.0f;   // Custom effect-specific

    // Packed as 4x float4 = 64 bytes (standard cbuffer size)
};

// ============================================================================
// COMPUTE EFFECT TYPE ENUMERATION
// ============================================================================

enum class ComputeEffectType {
    Temporal,      // Temporal accumulation, motion blur, trails
    Denoising,     // Denoise, bilateral filter
    PostFX,        // Tone mapping, color grading, filtering
    Custom,        // User-defined compute effect
};

// ============================================================================
// COMPUTE SHADER EFFECT DEFINITION
// ============================================================================

struct ComputeShaderEffect {
    // Identity
    std::string name;
    std::string description;
    ComputeEffectType type = ComputeEffectType::Custom;

    // Source
    std::string computeShaderCode;    // HLSL compute shader source
    std::string computeShaderPath;    // Path to shader file (optional)

    // Entry point (default: "main")
    std::string entryPoint = "main";

    // Thread group size (default: 8x8x1)
    uint32_t threadGroupX = 8;
    uint32_t threadGroupY = 8;
    uint32_t threadGroupZ = 1;

    // Texture I/O
    struct TextureBinding {
        std::string name;    // "CurrentFrame", "HistoryBuffer", etc.
        int slot;           // Register slot (t0, t1, etc.)
        bool isInput;       // true = SRV, false = UAV (output)
    };
    std::vector<TextureBinding> bindings;

    // Runtime state
    bool enabled = true;
    bool isDirty = true;
    ComPtr<ID3D12PipelineState> pipelineState;
    std::string lastCompiledCode;

    // Optional: History buffers for temporal effects
    int historyIndex = 0;
    bool historyInitialized = false;
    std::vector<ComPtr<ID3D12Resource>> historyTextures;
    int historyCount = 0;  // 0 = no history needed
};

// ============================================================================
// COMPUTE EFFECT MANAGER
// ============================================================================

class ComputeEffectManager {
public:
    ComputeEffectManager();
    ~ComputeEffectManager();

    // Register and manage effects
    bool AddEffect(const ComputeShaderEffect& effect);
    bool RemoveEffect(const std::string& effectName);
    ComputeShaderEffect* GetEffect(const std::string& effectName);

    // Compilation
    bool CompileEffect(const std::string& effectName, std::vector<std::string>& outErrors);

    // Execution
    bool ExecuteEffect(ID3D12GraphicsCommandList* commandList,
                      const std::string& effectName,
                      ID3D12Resource* inputTexture,
                      ID3D12Resource*& outputTexture,
                      const ComputeEffectParameters& params,
                      uint32_t width, uint32_t height);

    // Buffer management
    bool AllocateHistoryBuffers(const std::string& effectName, uint32_t width, uint32_t height);

private:
    std::vector<ComputeShaderEffect> m_effects;
    Device* m_device = nullptr;
    ShaderCompiler* m_compiler = nullptr;
    ComPtr<ID3D12RootSignature> m_genericRootSignature;

    bool CreateGenericRootSignature();
    bool CompileComputeShader(const std::string& source,
                             const std::string& entryPoint,
                             std::vector<uint8_t>& outBytecode,
                             std::vector<std::string>& outErrors);
};

} // namespace ShaderLab
