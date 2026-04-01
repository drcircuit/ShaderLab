#pragma once

#include <string>
#include <vector>
#include <cstddef>
#include <d3d12.h>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

namespace ShaderLab {

enum class TextureType { Texture2D, TextureCube, Texture3D };
enum class BindingType { Scene, File };
enum class AudioType { Music, OneShot };
enum class RenderAspectRatioPreset : uint8_t { Ratio_16_9 = 0, Ratio_1_1 = 1, Ratio_16_10 = 2, Ratio_4_3 = 3 };
enum class FullscreenRenderResolutionPreset : uint8_t {
    Full = 0,
    W4096 = 1,
    W2560 = 2,
    W2048 = 3,
    W1920 = 4,
    W1600 = 5,
    W1280 = 6,
    W1024 = 7,
    W800 = 8,
    W640 = 9
};

struct TextureBinding {
    int channelIndex = 0;
    bool enabled = false;
    BindingType bindingType = BindingType::Scene;
    int sourceSceneIndex = -1; 
    std::string filePath;
    ComPtr<ID3D12Resource> textureResource;
    bool fileTextureValid = false;
    TextureType type = TextureType::Texture2D; 
};

struct AudioClip {
    std::string name;
    std::string path;
    AudioType type = AudioType::Music;
    float bpm = 120.0f;
};

struct TrackerRow {
    int rowId = 0; 
    int sceneIndex = -1;
    std::string transitionPresetStem;
    std::string transitionShaderPath;
    float transitionDuration = 1.0f; 
    float timeOffset = 0.0f; // Offset in beats to subtract from global time for this scene
    int musicIndex = -1; 
    int oneShotIndex = -1; 
    bool isBeat = true; 
    bool stop = false; 
};

struct DemoTrack {
    std::string name = "Untitled Track";
    float bpm = 120.0f;
    int lengthBeats = 128; 
    std::vector<TrackerRow> rows;
    int currentBeat = 0;
    int lastTriggeredBeat = -1;
};

enum class TransportState { Stopped, Playing, Paused };

struct Transport { // Renamed from PreviewTransport for genera use
    TransportState state = TransportState::Stopped;
    double timeSeconds = 0.0;
    double lastFrameWallSeconds = 0.0;
    bool freezeTime = false;
    bool freezeBeat = false;
    float bpm = 140.0f;
};

struct Scene {
    std::string name;
    std::string description;
    std::string shaderCode;
    std::string shaderCodePath;
    std::vector<TextureBinding> bindings;
    TextureType outputType = TextureType::Texture2D;

    // ========================================================================
    // PIXEL SHADER BASED POST-FX EFFECT (Traditional)
    // ========================================================================
    struct PostFXEffect {
        std::string name;
        std::string shaderCode;
        std::string shaderCodePath;
        bool enabled = true;
        bool isDirty = true;
        std::string lastCompiledCode;
        std::string precompiledPath;
        ComPtr<ID3D12PipelineState> pipelineState;
        size_t compiledShaderBytes = 0;
        int historyIndex = 0;
        bool historyInitialized = false;
        std::vector<ComPtr<ID3D12Resource>> historyTextures;

        PostFXEffect() = default;
        PostFXEffect(const std::string& inName, const std::string& code)
            : name(inName), shaderCode(code) {}
    };

    // ========================================================================
    // COMPUTE SHADER BASED EFFECT (New - enables optimized algorithms)
    // ========================================================================
    struct ComputeEffect {
        enum class Type {
            Temporal,      // Temporal accumulation: motion blur, trails
            Denoising,     // Denoise, bilateral filter
            PostProcess,   // Tone mapping, color grading
            Custom         // User-defined
        };

        std::string name;
        std::string shaderCode;
        std::string shaderCodePath;
        std::string precompiledPath;
        Type type = Type::Custom;
        bool enabled = true;
        bool isDirty = true;
        std::string lastCompiledCode;

        // Compute shader parameters (standardized)
        // These map to cbuffer in compute shader
        float param0 = 0.0f;   // Decay, radius, threshold, etc.
        float param1 = 0.0f;   // Blend, intensity, strength, etc.
        float param2 = 0.0f;   // Saturation, falloff, speed, etc.
        float param3 = 0.0f;   // Custom effect-specific

        // Thread group dimensions
        uint32_t threadGroupX = 8;
        uint32_t threadGroupY = 8;
        uint32_t threadGroupZ = 1;

        // Entry point (default: "main")
        std::string entryPoint = "main";

        // Runtime data
        ComPtr<ID3D12PipelineState> pipelineState;
        size_t compiledShaderBytes = 0;
        int historyIndex = 0;
        bool historyInitialized = false;
        std::vector<ComPtr<ID3D12Resource>> historyTextures;
        int historyCount = 0;  // How many history frames this effect needs

        ComputeEffect() = default;
        ComputeEffect(const std::string& inName, Type inType, const std::string& code)
            : name(inName), type(inType), shaderCode(code) {}
    };

    // Both effect types in the processing chain
    std::vector<PostFXEffect> postFxChain;
    std::vector<ComputeEffect> computeEffectChain;
    
    // Runtime resources
    ComPtr<ID3D12Resource> texture;
    ComPtr<ID3D12DescriptorHeap> srvHeap;
    ComPtr<ID3D12DescriptorHeap> rtvHeap;
    ComPtr<ID3D12PipelineState> pipelineState;
    size_t compiledShaderBytes = 0;
    bool textureValid = false;
    bool isDirty = true;

    // Post FX runtime resources
    ComPtr<ID3D12Resource> postFxTextureA;
    ComPtr<ID3D12Resource> postFxTextureB;
    ComPtr<ID3D12DescriptorHeap> postFxSrvHeap;
    ComPtr<ID3D12DescriptorHeap> postFxRtvHeap;
    bool postFxValid = false;
    
    // Optional Precompiled Data
    std::string precompiledPath; 

    Scene() {}
    Scene(const std::string& n, const std::string& code) : name(n), shaderCode(code) {}
};

struct ProjectData {
    std::vector<Scene> scenes;
    std::vector<AudioClip> audioLibrary;
    DemoTrack track;
    Transport transport;
    RenderAspectRatioPreset renderAspectRatioPreset = RenderAspectRatioPreset::Ratio_16_9;
    FullscreenRenderResolutionPreset fullscreenRenderResolutionPreset = FullscreenRenderResolutionPreset::Full;
    std::string demoTitle;
    std::string demoAuthor;
    std::string demoDescription;
};

}
