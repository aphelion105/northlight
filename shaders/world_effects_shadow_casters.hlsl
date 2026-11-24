// Separate geometry pass: c0..3 contains the light's world->clip matrix.
row_major float4x4 LightMatrix:register(c0);
struct ShadowVertex {float4 position:POSITION0;float2 uv:TEXCOORD0;float depth:TEXCOORD1;};
ShadowVertex ShadowVS(float3 p:POSITION0,float3 n:NORMAL0,float2 uv:TEXCOORD0){
    ShadowVertex o;o.position=mul(float4(p,1),LightMatrix);o.uv=uv;o.depth=o.position.z;return o;
}
// Persistent directional cache only; paired with StaticCasterPS, which writes
// exact clamped fragment depth and rejects raw depths beyond the far plane.
ShadowVertex ShadowCacheVS(float3 p:POSITION0,float3 n:NORMAL0,float2 uv:TEXCOORD0){
    ShadowVertex o=ShadowVS(p,n,uv);o.position.z=saturate(o.position.z);return o;
}
float4 Material:register(c0); // rgb unused in depth pass, w alpha cutoff
float4 ShadowPS(float2 uv:TEXCOORD0,float depth:TEXCOORD1):COLOR0 {
    clip(tex2D(Scene,uv).a-Material.w);return depth.xxxx;
}
struct ShadowReplayOutput {float4 native:COLOR0;float4 depth:COLOR1;};
ShadowReplayOutput ShadowReplayPS(float2 uv:TEXCOORD0,float4 lightClip:TEXCOORD7,float4 diffuse:COLOR0) {
    ShadowReplayOutput output;output.native=lightClip.zzzz;output.depth=lightClip.z;
    float alpha=1;
    if(Material.w>=0){
        alpha=Material.x==2?diffuse.a:tex2D(Scene,uv).a;
        if(Material.x==1||Material.x==3)alpha*=diffuse.a;
        if(Material.y>.5){output.native.a=alpha;return output;}
        if(Material.x==3){
            // UV-anchored 4x4 Bayer coverage: native distance fade survives
            // shadow-map/camera movement; zero alpha never casts a rectangle.
            float2 cell=floor(uv*64),bit0=frac(cell*.5)*2,bit1=frac(floor(cell*.5)*.5)*2;
            float rank=frac((bit0.x*2+bit0.y*3)*.25)*16+frac((bit1.x*2+bit1.y*3)*.25)*4;
            clip(alpha-(rank+.5)/16);
        }else clip(alpha-Material.w);
    }
    return output;
}
// Union of a per-frame dynamic depth map (s0, working size) with a cached
// static depth map (s1, cachedSize) at an integer texel offset and an exact
// depth delta. A valid upstream caster remains an occluder when its shifted
// depth is below zero. Empty texels and geometry beyond the far plane stay 1.
float4 UnionOffset:register(c0); // cached texel offset x,y; depth delta; cached size in texels
float4 UnionSize:register(c1); // inverse working size, inverse cached size
float4 ShadowUnion(float2 uv:TEXCOORD0):COLOR0 {
    float2 texel=floor(uv/UnionSize.x);
    float own=tex2Dlod(Scene,float4((texel+.5)*UnionSize.x,0,0)).r;
    float2 cachedTexel=texel+UnionOffset.xy;
    float cached=1;
    if(all(cachedTexel>=0)&&all(cachedTexel<UnionOffset.w)){
        float c=tex2Dlod(Depth,float4((cachedTexel+.5)*UnionSize.y,0,0)).r;
        if(c<1){c+=UnionOffset.z;cached=c<=1?max(c,0):1;}
    }
    return min(own,cached).xxxx;
}

