// World pass inputs in D3D9 raster and view coordinates.
sampler2D Scene : register(s0);
sampler2D Depth : register(s1);
float4 ImageClip : register(c0); // invWidth, invHeight, near, far
float4 Projection : register(c1); // signed projection X,Y,Zsign,worldMinZ
float4 DepthParams : register(c2); // inverse world depth range,near radius,far radius,shadow texel
row_major float4x4 InverseView : register(c3);
float4 ScreenSize : register(c33); // full width,height; half-resolution width,height

float2 depthUV(float2 uv) {
    // D3D9 raster centers are integer pixels; texture centers are pixel+.5.
    // Choose the exact depth texel first, including in odd-sized half-res passes.
    float2 dimensions=ScreenSize.xy;
    return (clamp(floor(uv*dimensions),0,dimensions-1)+.5)*ImageClip.xy;
}
float normalizedDepth(float2 uv) {
    // Callers pass an explicit depth texel center, never a half-res boundary.
    return saturate((tex2Dlod(Depth,float4(uv,0,0)).r-Projection.w)*DepthParams.x);
}
// normalizedDepth through texld (1 slot; tex2Dlod costs 3-4), only before any flow control: Depth has one level.
float normalizedDepthFlat(float2 uv) {
    return saturate((tex2D(Depth,uv).r-Projection.w)*DepthParams.x);
}
float viewDistance(float d) {
    return ImageClip.z*ImageClip.w/max(ImageClip.w-d*(ImageClip.w-ImageClip.z),.00001);
}
float3 viewPositionDistance(float2 uv,float z) {
    float2 raster=uv-.5*ImageClip.xy;
    float2 xy=(raster*float2(2,-2)+float2(-1,1))/Projection.xy;
    return float3(xy,Projection.z)*z;
}
float3 viewPosition(float2 uv,float d) {return viewPositionDistance(uv,viewDistance(d));}
