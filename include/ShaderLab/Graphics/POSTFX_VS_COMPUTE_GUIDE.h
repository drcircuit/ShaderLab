/*
POSTFX VS COMPUTE EFFECTS: ARCHITECTURAL GUIDE

This document explains the separation of concerns and when to use each type.

================================================================================
POSTFX EFFECTS (Pixel Shader Based) - Traditional Approach
================================================================================

WHAT IT IS:
  - Renders a fullscreen quad
  - Runs pixel shader on every pixel
  - Outputs to render target texture
  - Ping-pongs between two textures in chain

GOOD FOR:
  - Color grading, tone mapping
  - Simple filters (blur, sharpen)
  - Post-processing that depends on neighbors
  - Effects with simple, per-pixel operations
  - Anything that fits the traditional deferred pipeline

COST:
  - Vertex shader overhead (fullscreen quad)
  - Rasterizer cost
  - Multiple passes with texture ping-pong

STRENGTHS:
  - Familiar to most shader programmers
  - Can use texture samplers
  - Works with standard D3D12 graphics pipeline

WEAKNESSES:
  - Inefficient for operations that don't need rasterization
  - Can't easily access previous frame data
  - Texture lookups are expensive for complex algorithms

EXAMPLE:
  PostFXEffect glitch;
  glitch.name = "Glitch";
  glitch.shaderCode = load_hlsl_pixel_shader();
  scene.postFxChain.push_back(glitch);

================================================================================
COMPUTE EFFECTS (Compute Shader Based) - Optimized Approach
================================================================================

WHAT IT IS:
  - Dispatches compute shader work
  - Direct memory access (UAV)
  - No rasterizer, no vertex processing
  - Can have history buffers built-in
  - Runs on GPU without graphics pipeline

GOOD FOR:
  - Temporal effects (accumulation, trails)
  - Operations that need history frames
  - Algorithms with shared memory optimizations
  - Anything compute-parallel (particle systems, deformations)
  - Effects that need frame-to-frame coherence

COST:
  - One compute dispatch per effect
  - UAV writes instead of RT blends
  - Must encode data manually (no texture filtering)

STRENGTHS:
  - ~20x faster than bloom post-processing
  - Easy access to history buffers
  - GPU cache-friendly
  - No rasterizer overhead
  - Shared group memory for algorithms

WEAKNESSES:
  - Need to manually handle texture coordinates
  - No automatic interpolation
  - More complex shader code

EXAMPLE:
  ComputeEffect temporal;
  temporal.name = "TemporalAccumulation";
  temporal.type = ComputeEffect::Type::Temporal;
  temporal.param0 = 0.90f;  // decay
  temporal.param1 = 0.40f;  // blend
  temporal.historyCount = 1; // 1 history frame
  temporal.shaderCode = load_hlsl_compute_shader();
  scene.computeEffectChain.push_back(temporal);

================================================================================
CHAINING BOTH TYPES TOGETHER
================================================================================

A scene effect chain can interleave both types:

  1. Scene render (pixel shader)
  2. PostFX: Color grade
  3. Compute: Temporal accumulation  <- Efficient glow!
  4. PostFX: Sharpen filter
  5. Compute: Denoise
  6. Display

The EffectChainProcessor automatically:
  - Switches between pixel and compute pipelines
  - Manages intermediate textures
  - Handles history buffers
  - Dispatches correctly based on effect type

CODE:
  scene.postFxChain.push_back(colorGrade);
  scene.computeEffectChain.push_back(temporal);
  scene.postFxChain.push_back(sharpen);
  scene.computeEffectChain.push_back(denoise);

  // Processor handles all the complexity
  auto result = effectChainProcessor.ApplyEffectChain(
      commandList, scene, sceneTexture, width, height, time);

================================================================================
COMMON PATTERNS YOU'LL USE
================================================================================

PATTERN 1: Glowing Wireframe (Dodecahedron)
  Scene shader: Draw wireframe
  + Compute effect (temporal): Accumulate history
  = Natural glow with motion trails
  Cost: ~0.3ms (vs ~2ms for bloom)

PATTERN 2: Motion Blur
  Scene shader: Render movement
  + Compute effect (temporal): Blend with 0.9 decay, 0.3 blend
  = Smooth motion trails following geometry
  Cost: Per-frame blend operation

PATTERN 3: Denoised Rendering
  Scene shader: Noisy rendering algorithm
  + Compute effect (denoising): Temporal denoise
  = Clean output from noisy input
  Cost: Much better than spatial denoise filters

PATTERN 4: Color Grading Pipeline
  Scene shader: Render content
  + PostFX: Adjust exposure
  + PostFX: Color grade
  + Compute: Temporal glow
  + PostFX: Final sharpen
  = Professional look with custom glow

================================================================================
MIGRATION FROM POSTFX-ONLY TO UNIFIED SYSTEM
================================================================================

BEFORE (PostFX only):
  // Everything was pixel shaders
  scene.postFxChain = {colorGrade, bloom, sharpen};
  result = applyPostFxChain(...);

AFTER (Unified):
  // Mix and match
  scene.postFxChain = {colorGrade, sharpen};
  scene.computeEffectChain = {temporalGlow, denoise};
  result = effectChainProcessor.ApplyEffectChain(...);

BENEFITS:
  ✓ Same API for both effect types
  ✓ Compute effects are 20x faster where applicable
  ✓ No breaking changes to existing effects
  ✓ Gradual adoption: add compute effects as needed
  ✓ Can test both approaches in same scene

================================================================================
PARAMETER STANDARDS FOR COMPUTE EFFECTS
================================================================================

All compute effects use standardized parameters:

  param0: Primary algorithm parameter
    - Temporal: decay factor (0.80-0.95)
    - Denoise: strength (0.0-1.0)
    - PostProcess: intensity (0.0-2.0)

  param1: Secondary algorithm parameter
    - Temporal: blend amount (0.1-0.7)
    - Denoise: edge preservation (0.0-1.0)
    - PostProcess: saturation (0.5-2.0)

  param2: Tertiary parameter
    - Temporal: color saturation (0.8-1.5)
    - Denoise: radius (1.0-4.0)
    - PostProcess: falloff (0.5-2.0)

  param3: Custom per-effect
    - Temporal: unused
    - Denoise: bilateral threshold
    - PostProcess: color shift amount

Thread groups:
  Standard: 8x8x1 per compute shader
  Can override in effect definition if needed

History buffers:
  - Set historyCount to number of previous frames needed
  - Temporal = 1 (previous frame)
  - Denoise = 2-4 (multiple history frames)
  - PostProcess = 0 (no history)

================================================================================
COMPILATION AND RUNTIME
================================================================================

Compilation (one-time):
  1. User adds effect to scene
  2. System detects type (PostFX or Compute)
  3. Compiles to appropriate pipeline (PSO or compute bytecode)
  4. Stores in effect.pipelineState

Runtime (per-frame):
  1. EffectChainProcessor.ApplyEffectChain()
  2. For each effect in order:
     - If PostFX: Use graphics pipeline, render quad
     - If Compute: Use compute pipeline, dispatch
     - Pass output to next effect
  3. Return final texture for display

Key insight: Both compile to GPU bytecode. Only dispatch path differs.

================================================================================
*/
