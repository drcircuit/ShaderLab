/*
================================================================================
SHADERLAB EFFECT SYSTEM ARCHITECTURE - UNIFIED DESIGN
================================================================================

GOAL:
  Enable both pixel shader and compute shader effects in a single chain,
  allowing users creativity while maintaining performance optimization.

================================================================================
LAYER 1: DATA DEFINITIONS (ShaderLabData.h)
================================================================================

Scene now contains:
  
  struct PostFXEffect {
    // Traditional pixel shader effect
    string code;
    float param0, param1, param2, param3;  // User parameters
    PSO* pipelineState;
    history buffers[];
  };

  struct ComputeEffect {
    // New compute shader effect
    enum Type { Temporal, Denoising, PostProcess, Custom };
    string code;
    Type type;
    float param0, param1, param2, param3;  // Standardized parameters
    int threadGroupX, threadGroupY;
    int historyCount;
    PSO* pipelineState;
    history buffers[];
  };

  vector<PostFXEffect> postFxChain;
  vector<ComputeEffect> computeEffectChain;

WHY THIS WORKS:
  - Both have the same lifecycle (compile, execute, cleanup)
  - Both can have history buffers
  - Both have parameters users can tweak
  - Serialization is independent

================================================================================
LAYER 2: SERIALIZATION (Serializer.cpp)
================================================================================

Project JSON format:
  {
    "scenes": [
      {
        "name": "Scene1",
        "code": "...",
        "postfx": [
          { "name": "ColorGrade", "code": "..." }
        ],
        "compute": [  // <-- NEW
          {
            "name": "TemporalGlow",
            "type": 0,  // Temporal
            "param0": 0.90,
            "param1": 0.40,
            "historyCount": 1,
            "code": "..."
          }
        ]
      }
    ]
  }

SERIALIZATION PATH:
  1. Load JSON
  2. Deserialize postfx[] → Scene.postFxChain
  3. Deserialize compute[] → Scene.computeEffectChain
  4. Both chain vectors exist independently
  5. Save reverses the process

================================================================================
LAYER 3: COMPILATION (EffectChainProcessor)
================================================================================

CompilePostFXEffect(effect):
  1. Take pixel shader code
  2. Add standard header (iTime, iResolution, iChannel0, iChannel1...)
  3. Compile to pixel shader bytecode
  4. Create PSO (graphics pipeline)
  5. Store in effect.pipelineState

CompileComputeEffect(effect):
  1. Take compute shader code
  2. Inject standard cbuffer (params, time, resolution)
  3. Compile to compute shader bytecode
  4. Create PSO (compute pipeline)
  5. Store in effect.pipelineState

KEY: Both go through same compilation flow!

================================================================================
LAYER 4: EXECUTION (EffectChainProcessor)
================================================================================

ApplyEffectChain(inputTexture):
  
  currentTexture = inputTexture
  
  for (effect in combined order)  // <-- Both chains interleaved
  
    if (effect is PostFXEffect):
      currentTexture = ApplyPostFXEffect(effect, currentTexture)
      
    else if (effect is ComputeEffect):
      currentTexture = ApplyComputeEffect(effect, currentTexture)
  
  return currentTexture

ApplyPostFXEffect(effect, input):
  1. Set render target to output texture
  2. Bind input as SRV
  3. Bind history textures as SRVs
  4. Draw fullscreen quad
  5. Pixel shader executes per-pixel
  6. Result goes to RT
  7. Copy result to history for next frame
  8. Return output texture

ApplyComputeEffect(effect, input):
  1. Create UAV for output texture
  2. Create UAV for next-frame history
  3. Bind input texture as SRV
  4. Bind history textures as SRVs
  5. Dispatch compute shader
     - Threads = (width+7)/8, (height+7)/8
     - Each thread handles one pixel
  6. Memory barrier
  7. Swap history buffers
  8. Return output texture

KEY DIFFERENCES:
  PostFX: Rasterizer + pixels
  Compute: GPU workload without rasterizer

================================================================================
LAYER 5: UI INTEGRATION (Not shown, but needed)
================================================================================

In scene editor:
  
  [Scene: MyScene]
  
  Post-FX Chain:
    ☐ Color Grade (PostFX)
      └ Parameters: ...
    ☐ Sharpen (PostFX)
      └ Parameters: ...
    ☐ Temporal Glow (Compute)  <-- NEW
      └ Decay: 0.90
      └ Blend: 0.40
    ☐ Denoise (Compute)  <-- NEW
      └ Strength: 0.80

The UI can detect effect type and show appropriate controls.

================================================================================
EXAMPLE: DODECAHEDRON WITH TEMPORAL GLOW
================================================================================

SETUP:
  Scene: "about_logo_dodecahedron"
  Scene shader: Renders rotating wireframe

  PostFX chain: (empty)
  
  Compute chain:
    - TemporalGlow
      type: Temporal
      param0 (decay): 0.92
      param1 (blend): 0.35
      param2 (saturation): 1.2
      historyCount: 1

EXECUTION EACH FRAME:
  1. Render scene: wireframe edges light up
  2. ApplyComputeEffect(TemporalGlow):
     - Read current pixels (bright wireframe)
     - Read history (dimmer previous frame)
     - Output = lerp(history * 0.92, current, 0.35)
     - Store output as new history
     - Display output
  3. Next frame: old position's glow is still there (decayed)

RESULT:
  ✓ Glowing motion trails
  ✓ Per-edge animation
  ✓ Natural accumulation
  ✓ 20x faster than bloom

================================================================================
ARCHITECTURE DIAGRAM
================================================================================

            ┌─────────────────────────────────────────┐
            │         Scene Definition                │
            │  (name, shader, bindings, effects)      │
            └─────────────────────────────────────────┘
                              │
                    ┌─────────┴─────────┐
                    │                   │
         ┌──────────▼─────────┐  ┌──────▼──────────┐
         │  PostFXEffect[]    │  │ ComputeEffect[]  │
         │  (pixel shaders)   │  │  (compute)       │
         └──────────┬─────────┘  └──────┬───────────┘
                    │                   │
                    └─────────┬─────────┘
                              │
                    ┌─────────▼──────────────┐
                    │ EffectChainProcessor   │
                    │ (unified dispatcher)   │
                    └─────────┬──────────────┘
                              │
                ┌─────────────┬─────────────┐
                │             │             │
         ┌──────▼──┐   ┌──────▼──┐   ┌──────▼──┐
         │ Graphics│   │ Compute  │   │ Memory  │
         │ PSO     │   │ PSO      │   │Barriers │
         └──────┬──┘   └──────┬───┘   └────┬────┘
                │             │             │
         ┌──────▼──────────────▼─────────────▼──────┐
         │        GPU Execution (per effect)        │
         │  Quad render OR compute dispatch         │
         └──────┬───────────────────────────────────┘
                │
         ┌──────▼──────────────┐
         │  Output Texture     │
         │  (to next effect)   │
         └─────────────────────┘

================================================================================
BENEFITS OF THIS DESIGN
================================================================================

FOR DEVELOPERS:
  ✓ Single unified ApplyEffectChain() interface
  ✓ Both types compile the same way
  ✓ Both types serialize the same way
  ✓ Can add compute effects incrementally
  ✓ Backward compatible (existing PostFX still works)

FOR USERS:
  ✓ Choose effect type per effect (not per scene)
  ✓ Chain them in any order
  ✓ Performance where it matters (temporal effects)
  ✓ Traditional approach for complex filters
  ✓ Easy tweaking with standardized parameters

FOR ARTISTS:
  ✓ See both effect types in single UI
  ✓ Reorder effects easily
  ✓ Compare performance (GPU time per effect)
  ✓ Parameter sliders for real-time tweaking

================================================================================
IMPLEMENTATION ROADMAP
================================================================================

PHASE 1 (Building):
  ✓ Add ComputeEffect struct to Scene
  ✓ Add serialization for ComputeEffect
  ✓ Create EffectChainProcessor
  ✓ Implement CompileComputeEffect
  ✓ Implement ApplyComputeEffect

PHASE 2 (Integration):
  ☐ Hook EffectChainProcessor into UISystem
  ☐ Hook EffectChainProcessor into DemoPlayer
  ☐ Create standard compute shader templates
  ☐ Add compute effect UI controls

PHASE 3 (Examples):
  ☐ Temporal accumulation shader
  ☐ Denoise shader
  ☐ Motion estimation shader
  ☐ Custom user effects

PHASE 4 (Polish):
  ☐ Performance metrics UI
  ☐ Shader debugging tools
  ☐ Error reporting for compute shaders
  ☐ History buffer visualization

================================================================================
*/
