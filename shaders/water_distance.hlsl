// Stock DXVK resolves the L8 target to the nearest UNORM8 fraction. Supported
// D3D9 sample counts divide sixteen, so every possible coverage is recovered.
sampler2D CoverageOrDistance : register(s0);
sampler2D ResolvedDistance : register(s1);

float sampleCoverage(float2 uv) {
    return floor(tex2D(CoverageOrDistance, uv).r * 16.0 + 0.5) / 16.0;
}

float4 WaterCorrectDistance(float2 uv : TEXCOORD0) : COLOR0 {
    float coverage = sampleCoverage(uv);
    // The R32F destination already holds the correct value at both endpoints.
    clip(min(coverage, 1.0 - coverage) - 1.0 / 32.0);
    return coverage >= 0.5 ? rcp(max(coverage, 0.5)) : 0.0;
}

float4 WaterMergeDistanceMS(float2 uv : TEXCOORD0) : COLOR0 {
    float coverage = sampleCoverage(uv);
    clip(coverage - 0.5);
    return float4(tex2D(ResolvedDistance, uv).r * rcp(max(coverage, 0.5)), 1, 0, 1);
}

float4 WaterMergeDistance(float2 uv : TEXCOORD0) : COLOR0 {
    float distance = tex2D(CoverageOrDistance, uv).r;
    clip(distance > 0 ? 1.0 : -1.0);
    return float4(distance, 1, 0, 1);
}
