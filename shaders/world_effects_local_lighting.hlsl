// Second pass to the same FP16 lighting buffer with COLORWRITEENABLE=RGB.
// Probes contain sky + bounce, so replace ambient instead of adding it twice.
// Missing probes use the analytic upper-sky fallback; resident cells replace
// it with measured GI, including cells with corners inside solid geometry.
struct GIOutput {float4 correction:COLOR0;float4 prelit:COLOR1;};
GIOutput WorldGI(float2 uv:TEXCOORD0) {
    GIOutput o;o.correction=0;o.prelit=0;
    float2 screen=uv;
    uv=halfDepthUV(uv);
    float d=normalizedDepth(uv);if(d>=.99999||waterDistance(uv,d)>0)return o;
    float4 smoothNormal=tex2Dlod(NormalBuffer,float4(screen,0,0));
    float3 correction=worldGIValue(worldPosition(uv,d),smoothNormal,false,o.prelit);
    o.correction=float4(correction,0);return o;
}
// Four POINT reads work on float32 textures without optional linear filtering.
// Empty metadata nodes must never interpolate their dummy ground=0 into real
// terrain heights. Density still fades with the ordinary bilinear weights.
float4 regionalFogAt(float2 uv,out float coverage) {
    float2 grid=uv*64-.5,base=floor(grid),f=grid-base;
    float4 a=tex2Dlod(RegionalFog,float4((base+.5)/64,0,0));
    float4 b=tex2Dlod(RegionalFog,float4((base+float2(1.5,.5))/64,0,0));
    float4 c=tex2Dlod(RegionalFog,float4((base+float2(.5,1.5))/64,0,0));
    float4 d=tex2Dlod(RegionalFog,float4((base+1.5)/64,0,0));
    float4 weights=float4((1-f.x)*(1-f.y),f.x*(1-f.y),(1-f.x)*f.y,f.x*f.y);
    float4 valid=weights*float4(a.w>0,b.w>0,c.w>0,d.w>0);
    float total=dot(valid,1),inv=1/max(total,1e-6);
    // Fade the field border inside the sampler so callers keep fewer registers.
    float edge=saturate(min(min(grid.x,grid.y),min(63-grid.x,63-grid.y))*.5);
    coverage=total*edge*edge*(3-2*edge);
    return float4(dot(valid,float4(a.x,b.x,c.x,d.x))*inv,
        dot(weights,float4(a.y,b.y,c.y,d.y)),dot(weights,float4(a.z,b.z,c.z,d.z)),
        dot(valid,float4(a.w,b.w,c.w,d.w))*inv);
}
// Direct light from the nearest authored local sources (lanterns, braziers,
// fires) on nearby surfaces. The game only glows the lamp model itself; this
// lights the ground and walls around it. The separate point passes subtract
// exactly the occluded contribution of each selected shadow-casting source.
float4 localDirectValue(float2 uv,int lightCount) {
    float2 halfUV=uv;
    uv=depthUV(uv);
    float d=normalizedDepth(uv);
    if(d>=.99999||waterDistance(uv,d)>0)return 0;
    float3 smoothNormal=tex2Dlod(NormalBuffer,float4(halfUV,0,0)).xyz;
    // r47 K4: lamps read weaker in direct sun (30%). The first lighting pass (the sun, by day) left
    // its visibility in the baseline alpha (s12); LocalLightInfo.z is the share removed in full sun,
    // faded with the sun's elevation (0 at night, with shadows off or without an active sun).
    // Sun shadow and interiors keep 1.
    float sunlit=saturate((tex2Dlod(BaselineLighting,float4(halfUV,0,0)).a-(1-GridInfo.z))/max(GridInfo.z,.001));
    float3 p=worldPosition(uv,d);float3 n=autoBumpNormal(uv,p,smoothNormal,viewDistance(d),InverseView[0].xyz,InverseView[1].xyz,InverseView[2].xyz);
    float3 result=0;
    // Unused slots are zero constants (reach 0), so no per-light branch is needed.
    // LocalLightColor.w is the precomputed 1/(end-start).
    [unroll]for(int i=0;i<lightCount;++i){
        float3 toLight=LocalLightPos[i].xyz-p;
        float inv=rsqrt(max(dot(toLight,toLight),.0025));
        float reach=saturate((LocalLightPos[i].w-1/inv)*LocalLightColor[i].w);
        float lambert=saturate(dot(n,toLight)*inv);
        result+=LocalLightColor[i].rgb*(reach*reach*lambert);
    }
    return float4(result*(LocalLightInfo.y*(1-LocalLightInfo.z*sunlit)),0);
}
float4 LocalDirect(float2 uv:TEXCOORD0):COLOR0{return localDirectValue(uv,8);}
float4 LocalDirect1(float2 uv:TEXCOORD0):COLOR0{return localDirectValue(uv,1);}
// Sixteen lamps in one pass (local_light_batches.h wide(): two neighbouring 8-lamp batches whose joint rect costs less than
// both): the same term as LocalDirect per lamp, the lamps in entry uniforms c84..c99 (position, w attenuation end) and
// c100..c115 (colour, w 1/(end-start)); c73..c78 retain material controls; unused slots are zero. c52 (LocalLightInfo.yz) and s12 as for LocalDirect.
float4 LocalDirect16(float2 uv:TEXCOORD0,uniform float4 LampPos[16]:register(c84),uniform float4 LampColor[16]:register(c100)):COLOR0 {
    float2 halfUV=uv;
    uv=depthUV(uv);
    float d=normalizedDepth(uv);if(d>=.99999||waterDistance(uv,d)>0)return 0;
    float3 smoothNormal=tex2Dlod(NormalBuffer,float4(halfUV,0,0)).xyz;
    float sunlit=saturate((tex2Dlod(BaselineLighting,float4(halfUV,0,0)).a-(1-GridInfo.z))/max(GridInfo.z,.001));
    float3 p=worldPosition(uv,d);float3 n=autoBumpNormal(uv,p,smoothNormal,viewDistance(d),InverseView[0].xyz,InverseView[1].xyz,InverseView[2].xyz);
    float3 result=0;
    [unroll]for(int i=0;i<16;++i){
        float3 toLight=LampPos[i].xyz-p;
        float inv=rsqrt(max(dot(toLight,toLight),.0025));
        float reach=saturate((LampPos[i].w-1/inv)*LampColor[i].w);
        float lambert=saturate(dot(n,toLight)*inv);
        result+=LampColor[i].rgb*(reach*reach*lambert);
    }
    return float4(result*(LocalLightInfo.y*(1-LocalLightInfo.z*sunlit)),0);
}
// Only batches containing an actual discharge use the inverse-square variant.
float4 LocalFlash(float2 uv:TEXCOORD0):COLOR0 {
    float2 halfUV=uv;
    uv=depthUV(uv);
    float d=normalizedDepth(uv);
    if(d>=.99999||waterDistance(uv,d)>0)return 0;
    float3 smoothNormal=tex2Dlod(NormalBuffer,float4(halfUV,0,0)).xyz;
    // r47 K4: lamps read weaker in direct sun (30%). The first lighting pass (the sun, by day) left
    // its visibility in the baseline alpha (s12); LocalLightInfo.z is the share removed in full sun,
    // faded with the sun's elevation (0 at night, with shadows off or without an active sun).
    // Sun shadow and interiors keep 1.
    float sunlit=saturate((tex2Dlod(BaselineLighting,float4(halfUV,0,0)).a-(1-GridInfo.z))/max(GridInfo.z,.001));
    float3 p=worldPosition(uv,d);float3 n=autoBumpNormal(uv,p,smoothNormal,viewDistance(d),InverseView[0].xyz,InverseView[1].xyz,InverseView[2].xyz);
    float3 result=0;
    // Unused slots are zero constants (reach 0), so no per-light branch is needed.
    // Positive color.w is 1/(end-start); negative is a transient source core squared.
    [unroll]for(int i=0;i<8;++i){
        float3 toLight=LocalLightPos[i].xyz-p;
        float inv=rsqrt(max(dot(toLight,toLight),.0025));
        float reach=saturate((LocalLightPos[i].w-1/inv)*LocalLightColor[i].w);
        float energy=reach*reach*(1-LocalLightInfo.z*sunlit);
        [branch]if(LocalLightColor[i].w<0){
            float d2=dot(toLight,toLight),core2=-LocalLightColor[i].w;
            float edge=saturate(1-d2/max(LocalLightPos[i].w*LocalLightPos[i].w,.001));
            energy=edge*edge*core2/(d2+core2);
        }
        float lambert=saturate(dot(n,toLight)*inv);
        result+=LocalLightColor[i].rgb*(energy*lambert);
    }
    return float4(result*LocalLightInfo.y,0);
}
// A single sky discharge owns the same two directional cascades as the sun.
// Subtract only this source's occluded radiance; ambient/GI and other lamps stay.
float4 FlashShadow(float2 uv:TEXCOORD0):COLOR0 {
    float2 screen=uv;uv=depthUV(uv);float d=normalizedDepth(uv);
    if(d>=.99999||waterDistance(uv,d)>0)return 0;
    float4 normal=tex2Dlod(NormalBuffer,float4(screen,0,0));
    float3 p=worldPosition(uv,d);
    float3 n=autoBumpNormal(uv,p,normal.xyz,viewDistance(d),InverseView[0].xyz,InverseView[1].xyz,InverseView[2].xyz);
    float3 toLight=LocalLightPos[0].xyz-p;float d2=dot(toLight,toLight);
    float core2=-LocalLightColor[0].w;
    float edge=saturate(1-d2/max(LocalLightPos[0].w*LocalLightPos[0].w,.001));
    float energy=edge*edge*core2/(d2+core2);
    float lambert=saturate(dot(n,toLight)*rsqrt(max(d2,.0025)));
    float visibility=directionalShadow(p,normal.xyz,normal.w);
    return float4(-LocalLightColor[0].rgb*(energy*lambert*LocalLightInfo.y*(1-visibility)),0);
}
