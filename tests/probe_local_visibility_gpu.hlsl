// The runner prepends the production world_effects.hlsl, including visibility.
sampler2D Queries : register(s1);
float4 VisibilityQuery(float2 uv:TEXCOORD0):COLOR0 {
    float4 q=tex2Dlod(Queries,float4(uv,0,0));
    return localProbeVisibility(q.w,q.xyz);
}
