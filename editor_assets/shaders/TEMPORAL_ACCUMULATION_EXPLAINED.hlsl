// VISUAL EXPLANATION OF TEMPORAL ACCUMULATION
// 
// What you see in the final image is the RESULT of this mathematical process
//
// ============================================================================
// THE MATH (showing 5 frames for example)
// ============================================================================
//
// Frame 1: Wireframe appears at position 1
//   Current:  [wireframe at pos 1]  = bright yellow (1.0, 1.0, 0)
//   History:  [empty]               = black (0, 0, 0)
//   Output:   lerp(black, yellow, 0.4) = 0.4 yellow
//   Store as history for next frame
//
// Frame 2: Wireframe has rotated, now at position 2
//   Current:  [wireframe at pos 2]  = bright yellow (1.0, 1.0, 0)
//   History:  [0.4 yellow at pos 1] = from previous frame
//   Decay:    [history * 0.9] = 0.36 yellow at pos 1
//   Output:   lerp(0.36 yellow at pos 1, bright yellow at pos 2, 0.4)
//            = you see BOTH positions! pos 1 is dimmer, pos 2 is bright
//   Store as history for next frame
//
// Frame 3: Wireframe continues rotating
//   Current:  [wireframe at pos 3]  = bright yellow
//   History:  [color at pos 1 and 2] = dimmer and dimmer
//   Decay:    [dimmer again]
//   Output:   Blend shows current bright + previous frames dim
//            = MOTION TRAIL EFFECT!
//
// ============================================================================
// WHY THIS CREATES THE GLOW EFFECT
// ============================================================================
//
// The wireframe edges give you sharp bright lines on current frame
// But edges are thin and move every frame
//
// Historical data accumulates around the path the edges took
// So you get a "smeared" glow where geometry was recently
//
// Example at a single pixel:
//   History (5 frames ago):  0.1 brightness
//   History (4 frames ago):  0.15             } These
//   History (3 frames ago):  0.25             } accumulate
//   History (2 frames ago):  0.35             } into visible
//   History (1 frame ago):   0.60             } glow halo
//   Current frame:           1.0 (on edge)
//   
//   Total visible = all of these stacked = bright core with glowing halo
//
// ============================================================================
// PSEUDO CODE: THE COMPLETE FLOW EACH FRAME
// ============================================================================

/*

VERTEX SHADER: (same for every pixel, runs once)
output vertex positions for fullscreen quad

PIXEL SHADER: (runs for EVERY pixel on screen)
for each pixel:
if (pixel is on a wireframe edge):
output = bright glow color
else:
output = black (0,0,0)
// Only outputs THIS FRAME's geometry

COMPUTE SHADER: (runs for EVERY pixel on screen)
for each pixel:
current = pixel from pixel shader (only this frame)
history = pixel from last frame's history buffer

// Fade out the history slightly
decayed = history * 0.90

// Mix new with old
blended = mix(decayed, current, 0.4)

// Output for display
screen[pixel] = blended

// Store for next frame
history_next[pixel] = blended

CPU/GPU SYNC:
Swap history buffers
history = history_next  // For next frame

*/

// ============================================================================
// PRACTICAL EXAMPLE: ONE PIXEL'S JOURNEY
// ============================================================================

/*
Let's trace one pixel on the screen over time.
Imagine the wireframe edge moves past it.

FRAME 1:
Pixel is empty (no edge here yet)
Pixel history = black (0, 0, 0)
Pixel current = black (0, 0, 0)
Compute: output = lerp(black*0.9, black, 0.4) = black
Screen shows: black

FRAME 2: (wireframe edge moves close)
Edge is 5 pixels away, faint glow tail reaches it
Pixel current = dim yellow (0.2, 0.2, 0)
Pixel history = black from before
Compute: output = lerp(black*0.9, 0.2yellow, 0.4) = 0.08yellow
Screen shows: very dim yellow
(History for next frame = 0.08yellow)

FRAME 3: (edge gets closer)
Edge is 2 pixels away
Pixel current = brighter yellow (0.6, 0.6, 0)
Pixel history = 0.08yellow from last frame
Decay history: 0.08 * 0.9 = 0.072yellow
Compute: output = lerp(0.072yellow, 0.6yellow, 0.4) 
    = 0.24 yellow + some from history
    = more visible glow
Screen shows: visible yellow glow

FRAME 4: (EDGE PASSES THROUGH THIS PIXEL!)
Pixel current = BRIGHT YELLOW (1.0, 1.0, 0) -- on the wireframe!
Pixel history = 0.24yellow from last frame
Decay history: 0.24 * 0.9 = 0.216yellow
Compute: output = lerp(0.216yellow, 1.0yellow, 0.4)
    = 0.216 + 0.4 from new
    = 0.616 + some history contribution (actually 0.6*0.6=0.36 in lerp)
    = very bright yellow
Screen shows: BRIGHT VISIBLE GLOW PEAK!

FRAME 5: (edge moves away)
Pixel current = black (0, 0, 0) -- edge is gone
Pixel history = bright yellow from when edge was here
Decay history: 0.616 * 0.9 = 0.55yellow (still quite bright!)
Compute: output = lerp(0.55yellow, black, 0.4)
    = 0.33yellow (history dominates because current is black)
Screen shows: glowing afterimage of the edge

FRAME 6:
Pixel current = black
Pixel history = 0.33yellow
Decay: 0.33 * 0.9 = 0.297yellow
Compute: output = lerp(0.297, black, 0.4)
    = 0.178yellow
Screen shows: dimmer afterimage

FRAME 20:
Still faintly glowing from where that edge passed weeks ago!
Eventually decays to black again

*/

// ============================================================================
// KEY INSIGHT
// ============================================================================
//
// You're not actually RENDERING glow bloom—you're AVERAGING HISTORY!
//
// The eye sees:
//  - Bright core where edge is NOW (current frame)
//  - Dimmer halo where edge WAS (accumulated history)
//  - Smooth falloff as you go further back in time (decay*decay*decay...)
//
// This LOOKS like a physically-inspired glow, but it's really just
// temporal integration. It's WAY cheaper than:
//  - Bloom passes (multiple blur iterations)
//  - Glow maps and screen-space bloom
//  - Multiple draws of the same geometry with additive blending
//
// ============================================================================
// COMPARISON: DIFFERENT SETTINGS
// ============================================================================

/*
SETTINGS: decay=0.95, blend=0.3 (very long trails)
Output: Very ghostly, lots of afterimage, very smooth motion
Best for: Light painting, long motion streaks, dreamy effects

SETTINGS: decay=0.90, blend=0.4 (normal motion blur)
Output: Balanced, trails are smooth but responsive
Best for: Your dodecahedron glow (what you want)

SETTINGS: decay=0.80, blend=0.5 (snappy response)
Output: Shorter trails, quicker fade, more responsive
Best for: Faster movements, less ghosting

SETTINGS: decay=0.70, blend=0.7 (almost no trails)
Output: Very short afterimage, almost like multiple frames of exposure
Best for: Very responsive, minimal blur effect
*/

float4 main(float2 uv, float4 current, float4 history, float decay, float blend) {
    // This is the one-liner that makes it all work:
    return lerp(history * decay, current, blend);
}
