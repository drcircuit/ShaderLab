#pragma once
/*
TEMPORAL ACCUMULATION RENDERER - C++ Integration Guide

This demonstrates how to add temporal accumulation to ShaderLab's rendering pipeline.
The goal is to create smooth motion trails and persistent glow without expensive bloom.

ARCHITECTURE:
    1. Frame N: Render wireframe to "current" texture via pixel shader
    2. Use compute shader to blend: output = lerp(history, current, blend)
    3. Copy output back to "history" texture for frame N+1
    4. Display output on screen

This is much more efficient than:
    - Bloom post-processing (multiple blurs)
    - Generating glow through expensive texture lookups
    - Rendering wireframe multiple times

KEY INSIGHT:
When you see a glowing wireframe with trails, you're seeing:
- Bright pixels where the wireframe is THIS frame
- Dimmer pixels where it WAS in previous frames
- This temporal data is what your eye integrates as "motion" and "glow"
*/

#include <d3d12.h>
#include <wrl/client.h>
#include <cstdint>

using Microsoft::WRL::ComPtr;

namespace ShaderLab {

class Device;

// Settings for temporal accumulation effect
struct TemporalSettings {
    float decayFactor;      // 0.85-0.95: keeps previous frame data
    float blendAmount;      // 0.3-0.7: how much new frame influences output
    float saturation;       // Color boost
    int padding;
};

class TemporalAccumulator {
public:
    TemporalAccumulator();
    ~TemporalAccumulator();

    // Initialize with device and dimensions
    bool Initialize(Device* device, uint32_t width, uint32_t height);
    void Shutdown();

    // Resize buffers if resolution changes
    bool Resize(uint32_t width, uint32_t height);

    // Apply temporal accumulation
    // - currentFrame: texture from pixel shader render
    // - commandList: for compute shader dispatch
    // - outResult: the accumulated output to display
    bool Accumulate(ID3D12GraphicsCommandList* commandList,
                    ID3D12Resource* currentFrame,
                    ID3D12Resource*& outResult,
                    const TemporalSettings& settings);

    // Reset history buffer (useful when effect parameters change)
    void ResetHistory(ID3D12GraphicsCommandList* commandList);

private:
    bool CreateComputePipeline();
    bool CreateHistoryBuffers(uint32_t width, uint32_t height);

    Device* m_device = nullptr;
    uint32_t m_width = 0;
    uint32_t m_height = 0;

    // Ping-pong buffers: one for current history, one for next
    ComPtr<ID3D12Resource> m_historyBuffer0;
    ComPtr<ID3D12Resource> m_historyBuffer1;
    ComPtr<ID3D12Resource> m_currentHistoryBuffer;

    // Output from accumulation
    ComPtr<ID3D12Resource> m_accumulatedOutput;

    // Compute resources
    ComPtr<ID3D12PipelineState> m_computePSO;
    ComPtr<ID3D12RootSignature> m_rootSignature;
    ComPtr<ID3D12DescriptorHeap> m_descriptorHeap;

    // Constant buffer for settings
    ComPtr<ID3D12Resource> m_settingsBuffer;

    bool m_useBuffer0 = true;
};

} // namespace ShaderLab

/*
USAGE IN YOUR CODE:
=====================

In your scene/effect rendering code:

    // 1. Render wireframe to "currentFrame" texture with pixel shader
    renderer->Render(commandList, pso, currentFrame, ...);

    // 2. Apply temporal accumulation
    ID3D12Resource* accumulatedResult = nullptr;
    TemporalSettings settings;
    settings.decayFactor = 0.90f;    // Keep 90% of history
    settings.blendAmount = 0.4f;     // Mix in 40% new frame
    settings.saturation = 1.2f;      // Slightly boost colors
    
    temporalAccum.Accumulate(commandList, currentFrame, accumulatedResult, settings);

    // 3. Use accumulatedResult for display instead of currentFrame
    // This now has motion trails and persistent glow!

TWEAKING VALUES:
================

decayFactor:
    0.95 - Very long trails, ghostly effect
    0.90 - Normal motion blur feel
    0.85 - Short trails, snappier response
    0.80 - Very short trails, almost like instant

blendAmount:
    0.1 - Much more history, less responsive to new geometry
    0.3 - Balanced, classic motion blur
    0.5 - More responsive, sharper but trails visible
    0.7 - Very responsive, trails are subtle

saturation:
    1.0 - No boost, natural colors
    1.2 - 20% color boost, makes glow pop more
    1.5 - Strong boost, can look oversaturated

WHEN TO USE:
============
✓ Glowing wireframe effects (like your dodecahedron)
✓ Motion trails
✓ Light painting effects
✓ Persistent geometry that fades
✓ Any effect where you want smoother trails than a single frame

✗ Effects that need frame-perfect precision
✗ Effects with rapid geometry changes
✗ Anything where temporal ghosting would break the illusion
*/
