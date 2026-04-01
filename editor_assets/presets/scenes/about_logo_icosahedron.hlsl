// Inputs assumed available: float iTime; float2 iResolution;

static const float3 VERTICES[20] = {
    float3(0.0,  0.0,  1.0),
    float3(0.58, 0.33, 0.75),
    float3(0.0, -0.67, 0.75),
    float3(-0.58, 0.33, 0.75),
    float3(0.36, 0.87, 0.33),
    float3(0.93,-0.13, 0.33),
    float3(0.58,-0.75, 0.33),
    float3(-0.58,-0.75, 0.33),
    float3(-0.93,-0.13, 0.33),
    float3(-0.36, 0.87, 0.33),
    float3(0.58, 0.75,-0.33),
    float3(0.93, 0.13,-0.33),
    float3(0.36,-0.87,-0.33),
    float3(-0.36,-0.87,-0.33),
    float3(-0.93, 0.13,-0.33),
    float3(-0.58, 0.75,-0.33),
    float3(0.0,  0.67,-0.75),
    float3(0.58,-0.33,-0.75),
    float3(-0.58,-0.33,-0.75),
    float3(0.0,  0.0,-1.0)
};

static const int SEG[60] = {
    0, 2, 6, 5, 1,
    0, 3, 8, 7, 2,
    0, 1, 4, 9, 3,
    2, 7,13,12, 6,
    8,14,18,13, 7,
    6,12,17,11, 5,
    3, 9,15,14, 8,
    1, 5,11,10, 4,
    4,10,16,15, 9,
    19,18,14,15,16,
    19,17,12,13,18,
    19,16,10,11,17
};

float2x2 Rot2(float a) {
    float s = sin(a), c = cos(a);
    return float2x2(c, -s, s, c);
}

float SegmentDistance(float2 p, float2 a, float2 b) {
    float2 pa = p - a;
    float2 ba = b - a;
    float  h  = saturate(dot(pa, ba) / max(dot(ba, ba), 1e-8));
    return length(pa - ba * h);
}

float2 Project(float3 p, float cameraZ) {
    float denom = p.z - cameraZ;
    denom = (abs(denom) < 1e-4) ? (denom < 0 ? -1e-4 : 1e-4) : denom;
    return p.xy / denom;
}

float3 Spin2Axis(float3 p, float ax, float ay) {
    float2 yz = mul(Rot2(ax), p.yz);
    p.y = yz.x; p.z = yz.y;

    float2 zx = mul(Rot2(ay), p.zx);
    p.z = zx.x; p.x = zx.y;

    return p;
}

float4 main(float2 fragCoord, float2 iResolution, float iTime) {
    float2 R = iResolution.xy;
    float2 U = (2.0 * fragCoord - R) / R.y;

    const float cameraZ      = 1.7;
    const float lineWidthPx  = 1.5;
    const float speedX       = 0.9;
    const float speedY       = 1.5;

    float ax = iTime * speedX;
    float ay = iTime * speedY;

    float px = 1.0 / R.y;
    float halfW = max(lineWidthPx * px, 0.5 * px);

    float3 col = 0.0;

    float2 prev2 = 0.0;
    float2 first2 = 0.0;

    [unroll]
    for (int k = 0; k < 60; k++) {
        int idx = SEG[k];
        float3 p = VERTICES[idx];

        p = Spin2Axis(p, ax, ay);

        float2 p2 = Project(p, cameraZ);

        if (k % 5 == 0) {
            first2 = p2;
            prev2 = p2;
            continue;
        }

        float2 a = prev2;
        float2 b = (k % 5 == 4) ? first2 : p2;

        float d = SegmentDistance(U, a, b);
        float l = smoothstep(halfW, 0.0, d);

        float faceId = floor(k / 5.0);
        float edgeId = (k % 5);
        float t = frac(faceId * 0.13 + edgeId * 0.21 + iTime * 0.05);

        float3 base = float3(0.42, 1.0, 1.00);
        float3 hot  = float3(0, 0.25, 0.8);
        float3 edgeColor = lerp(base, hot, t);

        col += edgeColor * l;
        prev2 = p2;
    }

    col = saturate(col);
    float alpha = saturate(dot(col, float3(0.333, 0.333, 0.333)) * 1.5);
    return float4(col, alpha);
}
