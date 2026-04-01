#pragma once
// Temporal Accumulation Compute Shader
// This takes a current frame and an accumulated history,
// blends them together to create motion trails and persistent glow
//
// HOW IT WORKS:
// 1. Current frame has bright wireframe edges (but only on this frame)
// 2. History buffer has dimmed values from all previous frames
// 3. We blend them: output = lerp(history * decay, current, blend_amount)
// 4. Store output as new history for next frame
//
// BENEFIT: Creates smooth glow trails without expensive bloom post-processing
// The wireframe data accumulates naturally over time

// Constant buffer for control parameters
cbuffer TemporalSettings : register(b0) {
    float decayFactor;      // 0.85-0.95: How much history to keep (higher = longer trails)
    float blendAmount;      // 0.3-0.7: How much new frame to blend in
    float saturation;       // Boost colors slightly
    int padding;
};

// Current frame result (from pixel shader)
Texture2D<float4> currentFrame : register(t0);

// Accumulated history from previous frame  
Texture2D<float4> historyBuffer : register(t1);

// Output: next frame and new history
RWTexture2D<float4> outputFrame : register(u0);
RWTexture2D<float4> outputHistory : register(u1);

[numthreads(8, 8, 1)]
void main(uint3 dtid : SV_DispatchThreadID) {
    uint2 coord = dtid.xy;

    // Read current and history values
    float4 current = currentFrame[coord];
    float4 history = historyBuffer[coord];

    // This is the temporal accumulation formula:
    // - Decay history slightly (it gets darker over time)
    float4 decayedHistory = history * decayFactor;

    // - Blend new frame into decayed history
    float4 accumulated = lerp(decayedHistory, current, blendAmount);

    // Output both the accumulated value (as screen output)
    // and store it for next frame's history
    outputFrame[coord] = accumulated;
    outputHistory[coord] = accumulated;
}
