// SHADER TUTORIAL: How to Build a 3D Wireframe Effect in HLSL
//
// This shader demonstrates the core technique for drawing 3D geometry as glowing lines.
// You can use this same approach for any polyhedron: tetrahedron, cube, octahedron, etc.
//
// ============================================================================
// STEP 1: DEFINE YOUR 3D SHAPE
// ============================================================================
// Every 3D object starts with VERTICES (points in 3D space)
// A cube has 8 vertices, an icosahedron has 20, a dodecahedron has 20, etc.

static const float3 VERTICES_TETRAHEDRON[4] = {
    float3(1.0,  1.0,  1.0),
    float3(1.0, -1.0, -1.0),
    float3(-1.0,  1.0, -1.0),
    float3(-1.0, -1.0,  1.0)
};

// Then define EDGES (which vertices connect to form the wireframe)
// Each pair of numbers refers to vertex indices above
// Tetrahedron has 6 edges
static const int EDGES_TETRAHEDRON[12] = {
    0,1, 0,2, 0,3,  // From vertex 0 to others
    1,2, 1,3,       // From vertex 1 to others
    2,3             // From vertex 2 to others
};

// ============================================================================
// STEP 2: HELPER FUNCTIONS (these are the same for any shape)
// ============================================================================

// 2x2 rotation matrix - used to rotate objects in 3D space
float2x2 Rot2(float angle) {
    float s = sin(angle);
    float c = cos(angle);
    return float2x2(c, -s, s, c);
}

// This function answers: "How far is point P from a line segment from A to B?"
// Used to determine if a pixel should be drawn
float SegmentDistance(float2 p, float2 a, float2 b) {
    float2 pa = p - a;
    float2 ba = b - a;
    // saturate clamps value to 0-1 range
    float h = saturate(dot(pa, ba) / max(dot(ba, ba), 1e-8));
    return length(pa - ba * h);
}

// This converts 3D world coordinates to 2D screen coordinates
// Like a camera that takes a 3D photo and makes it 2D
float2 Project(float3 p, float cameraZ) {
    // cameraZ is how far back the camera is
    // Closer objects appear larger
    float denom = p.z - cameraZ;
    denom = (abs(denom) < 1e-4) ? (denom < 0 ? -1e-4 : 1e-4) : denom;
    return p.xy / denom;
}

// Rotate a 3D point around X and Y axes
// ax = how much to tilt up/down (pitch)
// ay = how much to spin left/right (yaw)
float3 Spin2Axis(float3 p, float ax, float ay) {
    float2 yz = mul(Rot2(ax), p.yz);
    p.y = yz.x;
    p.z = yz.y;

    float2 zx = mul(Rot2(ay), p.zx);
    p.z = zx.x;
    p.x = zx.y;

    return p;
}

// ============================================================================
// STEP 3: THE MAIN FUNCTION (where everything comes together)
// ============================================================================

float4 main(float2 fragCoord, float2 iResolution, float iTime) {
    // Convert pixel coordinates to a -1 to 1 scale (for easier math)
    float2 R = iResolution.xy;
    float2 U = (2.0 * fragCoord - R) / R.y;

    // CAMERA SETTINGS - Tweak these to change the look
    const float cameraZ      = 1.5;     // Move camera closer/farther
    const float lineWidthPx  = 1.5;   // How thick the edges are
    const float speedX       = 0.7;    // How fast it rotates around X
    const float speedY       = 1.2;    // How fast it rotates around Y

    // Calculate rotation angles based on time
    // This makes the shape spin continuously
    float ax = iTime * speedX;
    float ay = iTime * speedY;

    // Convert pixel width to the same scale as U
    float px = 1.0 / R.y;
    float halfW = max(lineWidthPx * px, 0.5 * px);

    // Start with black background
    float3 col = 0.0;

    // Loop through all edges of the tetrahedron
    // We have 6 edges * 2 vertices = 12 values in the loop
    [unroll]
    for (int k = 0; k < 12; k += 2) {
        // Get the two vertices that form this edge
        int idx1 = EDGES_TETRAHEDRON[k];
        int idx2 = EDGES_TETRAHEDRON[k + 1];

        float3 p1 = VERTICES_TETRAHEDRON[idx1];
        float3 p2 = VERTICES_TETRAHEDRON[idx2];

        // Rotate both vertices (make it spin)
        p1 = Spin2Axis(p1, ax, ay);
        p2 = Spin2Axis(p2, ax, ay);

        // Project both 3D points to 2D screen space
        float2 screen1 = Project(p1, cameraZ);
        float2 screen2 = Project(p2, cameraZ);

        // Find how close the current pixel is to this edge
        float distance = SegmentDistance(U, screen1, screen2);

        // Convert distance to a glow brightness
        // smoothstep: 0 (outside halfW), 1 (at center), and fades in between
        float brightness = smoothstep(halfW, 0.0, distance);

        // Animate the color based on which edge and what time
        // This creates the pulsing effect
        float edgeIndex = float(k / 2);
        float time_factor = frac(edgeIndex * 0.1 + iTime * 0.1);

        // Create a color that animates from cyan to blue
        float3 color_cool = float3(0.0, 1.0, 1.0);  // Cyan
        float3 color_hot = float3(0.0, 0.3, 1.0);   // Blue
        float3 edgeColor = lerp(color_cool, color_hot, time_factor);

        // Add this edge's contribution to the output
        col += edgeColor * brightness;
    }

    // Make sure colors don't exceed valid range
    col = saturate(col);

    // Create alpha (transparent/opaque) based on brightness
    float alpha = saturate(dot(col, float3(0.333, 0.333, 0.333)) * 1.5);

    return float4(col, alpha);
}

// ============================================================================
// HOW TO MODIFY FOR OTHER SHAPES:
// ============================================================================
// Replace VERTICES_TETRAHEDRON and EDGES_TETRAHEDRON with:
//
// CUBE (8 vertices, 12 edges):
//   8 vertices at (±1, ±1, ±1)
//   12 edges forming box outline
//
// OCTAHEDRON (6 vertices, 12 edges):
//   6 vertices at (±1,0,0), (0,±1,0), (0,0,±1)
//   12 edges forming 8 triangular faces
//
// DODECAHEDRON (20 vertices, 30 edges):
//   Mix of cube vertices and golden rectangle vertices
//   30 edges forming pentagonal faces
//
// ICOSAHEDRON (12 vertices, 30 edges):
//   Vertices based on golden rectangles
//   30 edges forming triangular faces
