// DODECAHEDRON SHADER - Educational walkthrough
// This shader draws a rotating dodecahedron with glowing edges
// 
// CONCEPT: A dodecahedron is a 12-sided polyhedron with pentagonal faces
// Unlike the icosahedron, it has straight faces you can see as you rotate it
//
// KEY TECHNIQUE: We draw edges of the 3D shape as glowing lines on screen
// - Project 3D vertices to 2D screen space
// - Draw lines between connected vertices
// - Add glow/bloom with color cycling

// Inputs assumed available: float iTime; float2 iResolution;

// The golden ratio - used to define dodecahedron geometry
static const float PHI = 1.61803398875;
static const float PHI_INV = 0.61803398875;

// 20 vertices of a dodecahedron
// These form the same symmetric structure as an icosahedron
// but with different connectivity
static const float3 VERTICES[20] = {
    // 8 cube vertices: (±1, ±1, ±1)
    float3(-1.0, -1.0, -1.0),
    float3(1.0, -1.0, -1.0),
    float3(1.0,  1.0, -1.0),
    float3(-1.0,  1.0, -1.0),
    float3(-1.0, -1.0,  1.0),
    float3(1.0, -1.0,  1.0),
    float3(1.0,  1.0,  1.0),
    float3(-1.0,  1.0,  1.0),

    // 12 golden rectangle vertices
    // (0, ±φ, ±1/φ) - left/right faces
    float3(0.0,  PHI,  PHI_INV),
    float3(0.0,  PHI, -PHI_INV),
    float3(0.0, -PHI,  PHI_INV),
    float3(0.0, -PHI, -PHI_INV),

    // (±1/φ, 0, ±φ) - top/bottom faces
    float3(PHI_INV,  0.0,  PHI),
    float3(-PHI_INV,  0.0,  PHI),
    float3(PHI_INV,  0.0, -PHI),
    float3(-PHI_INV,  0.0, -PHI),

    // (±φ, ±1/φ, 0) - front/back faces
    float3(PHI,  PHI_INV,  0.0),
    float3(PHI, -PHI_INV,  0.0),
    float3(-PHI,  PHI_INV,  0.0),
    float3(-PHI, -PHI_INV,  0.0)
};

// 30 edges of the dodecahedron defined as vertex index pairs
// Each edge connects two vertices that should have a glowing line
static const int EDGES[60] = {
    // Cube edges (12 edges)
    0,1, 1,2, 2,3, 3,0,  // back face
    4,5, 5,6, 6,7, 7,4,  // front face
    0,4, 1,5, 2,6, 3,7,  // connecting edges

    // Golden rectangle edges (form the pentagonal faces)
    8,9,   8,16,  8,13,  8,4,   8,7,
    9,15,  9,2,   9,3,   9,16,  9,18,
    10,11, 10,12, 10,13, 10,4,  10,5,
    11,14, 11,0,  11,1,  11,12, 11,19,
    12,14, 12,15, 14,17, 15,17, 17,18,
    18,19, 19,20, 19,3
};

// 2D rotation matrix - rotates a 2D point around origin
// Used for rotating the 3D object
float2x2 Rot2(float a) {
    float s = sin(a);
    float c = cos(a);
    return float2x2(c, -s, s, c);
}

// Calculate minimum distance from point P to line segment from A to B
// This is used to find how close a pixel is to an edge
float SegmentDistance(float2 p, float2 a, float2 b) {
    float2 pa = p - a;
    float2 ba = b - a;
    // Project p onto the line segment (clamped to 0-1)
    float h = saturate(dot(pa, ba) / max(dot(ba, ba), 1e-8));
    // Return distance to nearest point on segment
    return length(pa - ba * h);
}

// Project a 3D point to 2D screen space using perspective projection
// cameraZ is the camera's Z position (how far back the camera is)
float2 Project(float3 p, float cameraZ) {
    float denom = p.z - cameraZ;
    // Avoid divide by zero with a small epsilon
    denom = (abs(denom) < 1e-4) ? (denom < 0 ? -1e-4 : 1e-4) : denom;
    // Simple perspective: divide X,Y by distance to camera
    return p.xy / denom;
}

// Rotate a 3D point around X and Y axes
// ax = rotation angle around X axis (pitch)
// ay = rotation angle around Y axis (yaw)
float3 Spin2Axis(float3 p, float ax, float ay) {
    // Rotate around X axis (affects Y and Z)
    float2 yz = mul(Rot2(ax), p.yz);
    p.y = yz.x;
    p.z = yz.y;

    // Rotate around Y axis (affects Z and X)
    float2 zx = mul(Rot2(ay), p.zx);
    p.z = zx.x;
    p.x = zx.y;

    return p;
}

float4 main(float2 fragCoord, float2 iResolution, float iTime) {
    // Convert fragment coordinates to normalized coordinates (-1 to 1)
    float2 R = iResolution.xy;
    float2 U = (2.0 * fragCoord - R) / R.y;

    // Camera setup
    const float cameraZ      = 2.5;     // How far back the camera is
    const float lineWidthPx  = 1.5;    // Edge line thickness in pixels
    const float speedX       = 0.6;    // Rotation speed around X axis
    const float speedY       = 1.0;    // Rotation speed around Y axis

    // Calculate rotation angles based on time
    float ax = iTime * speedX;
    float ay = iTime * speedY;

    // Convert line width from pixels to screen space units
    float px = 1.0 / R.y;
    float halfW = max(lineWidthPx * px, 0.5 * px);

    // Initialize output color to black
    float3 col = 0.0;

    float2 prev2 = 0.0;
    float2 first2 = 0.0;

    // Draw all 30 edges
    [unroll]
    for (int k = 0; k < 60; k++) {
        // Get vertex index from EDGES array
        int idx = EDGES[k];

        // Get the 3D position of this vertex
        float3 p = VERTICES[idx];

        // Rotate the vertex
        p = Spin2Axis(p, ax, ay);

        // Project to 2D screen space
        float2 p2 = Project(p, cameraZ);

        // Start of a new edge (every 2 vertices)
        if (k % 2 == 0) {
            first2 = p2;
            prev2 = p2;
            continue;
        }

        // End of the edge - draw line from prev2 to p2
        float2 a = prev2;
        float2 b = p2;

        // How close is the current pixel to this edge line?
        float d = SegmentDistance(U, a, b);

        // Create glow: smooth fadeout from line width to 0
        float l = smoothstep(halfW, 0.0, d);

        // Animate colors based on edge index and time
        // This creates the pulsing/traveling light effect
        float edgeId = float(k / 2);
        float t = frac(edgeId * 0.07 + iTime * 0.08);

        // Color gradient: cyan base -> blue hot
        float3 base = float3(0.2, 1.0, 1.0);   // Bright cyan
        float3 hot = float3(0.0, 0.2, 0.8);    // Deep blue
        float3 edgeColor = lerp(base, hot, t);

        // Add this edge's contribution to the final color
        col += edgeColor * l;

        prev2 = p2;
    }

    // Clamp colors to valid range (0-1) to avoid oversaturation
    col = saturate(col);

    // Calculate alpha (transparency) based on brightness
    // Brighter parts are more opaque
    float alpha = saturate(dot(col, float3(0.333, 0.333, 0.333)) * 1.5);

    return float4(col, alpha);
}
