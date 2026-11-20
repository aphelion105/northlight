// Geometry-backed directional shadows, world-space probe GI and volumetric scattering.
// Inputs are independent of the currently visible screen geometry except receiver depth.
#include "world_view.hlsl"
sampler2D ShadowNear : register(s2);
sampler2D ShadowFar : register(s3);
sampler2D CurrentLocalLighting : register(s3); // WorldComposite: current local radiance after directional/GI filtering.
sampler2D ProbeR : register(s4);
sampler2D ProbeG : register(s5);
sampler2D ProbeB : register(s6);
sampler2D ProbeVisibility : register(s7);
sampler2D EffectCoverage : register(s7); // Final passes only: native alpha layers already in Scene.
float4 EffectCoverageInfo : register(c79);
// Final passes: neutral volume fog (x), local light (y), absent rain mask (z),
// zero native surface-fog term in an encoded nonnegative scene (w).
float4 CompositeWork : register(c80); // zero retains the complete calculation
float4 PrelitDaylightInfo : register(c81); // x: this frame produced the separate prelit daylight image.
sampler2D LightingBuffer : register(s8);
sampler2D PrelitDaylight : register(s14); // Prelit composite only: independent, shadowed celestial irradiance.
sampler2D ProbePrevious : register(s8); // 0.3.197: WorldGI only: previous SH R|G|B in three horizontal thirds of a 3n*n x n texture
sampler2D FogBuffer : register(s9);
sampler2D WaterMask : register(s11);
sampler2D BaselineLighting : register(s12);
// 0.3.209 (task 29): s12 also carries the actor shadow map (ActorShadow only, R32F, POINT, CLAMP) and the half-res actor
// visibility it writes (WorldLighting only: r sun, g moon, POINT, CLAMP). LocalDirect and TemporalLight rebind s12 to BaselineLighting.
sampler2D ActorMap : register(s12);
sampler2D ActorVisibility : register(s12);
sampler2D RegionalFog : register(s13); // ground, day extinction, night extra, layer height
sampler2D RainMask : register(s13); // 0.3.202 (rain mask): WorldComposite only, rain streak coverage (r), 0 without rain
sampler2D DitherNoise : register(s2); // WorldComposite only: 64x64 signed blue-noise offsets (rgb, +-1/255, POINT, WRAP; sky/dither_noise.h); ShadowNear's slot, idle by then
sampler2D HdrLinearBase : register(s2); // HDR composition: decoded/expanded legacy pixels and separately captured linear sky.
float4 RegionalFogInfo : register(c31); // world node0 XY, inverse field span, night fraction
float4 WaterInfo : register(c30);
float4 RemovalInfo : register(c30); // RemovalSmooth, TemporalLight: y 1 when a lit source is drawn, z 1/(summed source weight), w disc radius in half-res pixels at view distance 1
sampler2D ProbeMetadata : register(s10); // exact world-cell xyz; w: born time, -1 missing, -2 solved inside geometry
// 0.3.209 (task 29): ActorShadow only: the 64x64 tile minimum of the actor map (ActorTiles writes it; R32F, POINT, CLAMP)
sampler2D ActorTileMap : register(s10);
sampler2D AmbientOcclusion : register(s10); // 0.3.174 TemporalLight/WorldComposite: half-res contact AO (a), pre-weighted bloom (rgb), LINEAR; neutral (0,0,0,1) unless the world composites

row_major float4x4 NearMatrix : register(c7);
row_major float4x4 FarMatrix : register(c11);
float4 Camera : register(c15);
float4 TemporalReach : register(c15); // w: TemporalLight only, largest drawn source channel per unit of normalised visibility
float4 SunDirection : register(c16); // toward source
float4 DirectLight : register(c17); // w: directional maps can prove daylight access for prelit receivers
float4 AmbientLight : register(c18); // native ambient RGB, evening surface ambient gain in w
float4 GridOrigin : register(c19); // rgb: NightBrightness light per unit of albedo (WorldComposite only, 0 = off); w: world probe spacing
float4 GIResponse : register(c66); // x: prelit GI mix, y: brightness delta; GI only, before fog owns c66
float4 GridInfo : register(c20); // atlas N,GI intensity,shadow strength,probe ready
float4 FogInfo : register(c21); // regional field ready,direct volume gain,direct RGB bound,max distance
float4 FogColor : register(c22); // scattering albedo.rgb, generic-forest daytime air extinction
float4 LightCenter : register(c23);
float4 PassInfo : register(c24); // shadow bias,softness,debug,seconds
float4 LegacyFog : register(c25); // original VS c12.xyz, validated enabled flag
float4 SourcePolicy : register(c27); // first source, diagnostic energy weight, time, regional density
float4 LegacyDirection : register(c28);
float4 LegacyDirect : register(c29);
float4 LegacyFogColor : register(c26); // proven terrain PS3 c2/c6.rgb or fixed fog RGB
float4 VolumeAir : register(c32); // Dusk/STV extinction, Dusk/STV finite layer height
float4 BlurInfo : register(c34); // half-resolution texel step for the fog blur (x,y), unused
// Final composite only: c34 is rebound AFTER both blur passes (and on fog-off
// frames). Reuse the bank without changing other passes or the 68-register
// diagnostic capture format. Horizon haze extras live in free components.
float4 HorizonHaze : register(c34); // haze RGB (game fog colour), optical depth*log2(e) (0 = off)
float4 ShadowRange : register(c35); // inverse directional-shadow depth span, shared with CPU; yzw: horizon lift colour (WorldComposite only)
float4 LocalLightPos[8] : register(c36); // xyz position, w attenuation end
float4 LocalLightColor[8] : register(c44); // rgb diffuse, w 1/(attenuation end - start)
float4 LocalLightInfo : register(c52); // count, strength, share removed in direct daylight (LocalDirect)
sampler2D NormalBuffer : register(s14); // half-res smoothed world normal (xyz), confidence 0/1 (w, 0 at silhouettes); bound for the lighting passes
sampler2D SourceVisibility : register(s15); // 1x1: temporally smoothed clear-sky fraction over the source disc (fog passes)
sampler2D DirectionalVisibility : register(s15); // WorldLightingSoft only: resolved sun/moon PCSS.
sampler2D PreviousSourceVisibility : register(s15); // previous value (SourceVisibilityPS only)
float4 SourceInfo : register(c67); // disc tangent radius, history weight, horizon sun (zw)
float4 HorizonSun : register(c67); // zw: horizontal sun direction x lobe strength (WorldComposite only)
sampler2D LightHistory : register(s14); // previous frame's stabilized lighting buffer (temporal pass only)
sampler2D CurrentDistance : register(s15); // This frame's R32F view distance: WorldNormals/TemporalLight, never the previous frame.
sampler2D BumpMaterial : register(s9); // WorldLighting / LocalDirect only; FogBuffer is idle here.
float4 BumpControl : register(c73); // strength, near-detail fade distance
sampler2D MaterialFog : register(s4); // MaterialComposite only
sampler2D MaterialSource : register(s5); // native scene, before AO/fog composition
sampler2D MaterialOcclusion : register(s6); // sun visibility, moon visibility, cached cavity, coverage
float4 MaterialOcclusionInfo : register(c78); // z: HDR sky-only base; w: imported fog is linear radiance
float4 MaterialDirection[2] : register(c74);
float4 MaterialColor[2] : register(c76);
#include "auto_bump_normal.hlsl"
sampler2D DepthHistory : register(s15); // previous frame's linear view distance per half-res texel
sampler2D SmoothedLighting : register(s9); // 0.3.185 TemporalLight only: the LightingBuffer after RemovalSmooth (s9 is FogBuffer's slot, idle and rebound before its readers)
row_major float4x4 PreviousView : register(c53);
float4 TemporalInfo : register(c57); // history weight (0 disables), horizon shape (yzw)
float4 HorizonShape : register(c57); // yzw: terrain start view Z, 1/ramp (0 = sky only), log2(e)/sin(band) (WorldComposite only)
float4 FogRange : register(c58); // near fade start, 1/(fade length), lamp glow strength, lamp glow soft cap
float4 LocalLightFog[8] : register(c59); // x extinction at the light (LocalFog reads only .x of c59..c66), yzw unused there
// 0.3.198 (rain): c59.zw are the weather scalars (LocalLightFog[0].zw, free: only .x is read); z is 0 without weather and
// every use below reduces to the old math then (w is the exception by design: it carries the old literal .0017, see WorldFog). Written once per
// frame in the bank, read before the lamp fog batch overwrites c59..c62.
float4 WeatherInfo : register(c59); // z direct shadow softening (WorldLighting), w the shared air extinction .0017 + the extra (WorldFog)
// 0.3.199 (fog clouds): FogClouds only, host-folded (NorthlightFogClouds::shaderConstants), written only while the clouds are active (zero otherwise,
// as before). The .x components are the bank's until the lamp fog batches (a later pass) overwrite c59..c66 .x with LocalLightFog. c59.y -2.5 height;
// c60.yzw large-noise origin; c61.x the large noise below which no bank can be, c61.yzw small-noise origin; c62.x the tallest bank top (5.5 height),
// c62.y -threshold x sharpness, c62.z 8 height, c62.w sigmaMax; c63.x .35 sharpness, c63.yz 1/large and 1/small period, c63.w .65 sharpness.
float4 CloudInfo[4] : register(c60);
// 0.3.200 (gpu budget): c60.x (free in the cloud pass) and c64.z (FogTemporal reads only c64.y) lengthen the clouds' and WorldFog's world-fixed
// intervals at reduced GpuBudgetMs levels (fewer steps over the same 128 units); both 0 at full level, where mad(w,1/N,0) is the old w/N exactly.
float4 FogStepInfo : register(c64);
float4 HorizonEdge : register(c64); // w: the camera's far edge, sine of its elevation (WorldComposite only; apparent_horizon.h)
sampler3D CloudNoise : register(s14); // tileable N^3 L8 volume (LINEAR, WRAP); s14 is NormalBuffer/LightHistory in other passes, never bound together with this one

float waterDistance(float2 uv,float d) {
    if(WaterInfo.x<.5)return 0;
    float2 m=tex2Dlod(WaterMask,float4(uv,0,0)).rg;
    return m.x>ImageClip.z && m.x<=viewDistance(d)+max(.012,m.x*.0007) && m.y>.003?m.x:0;
}
float receiverDistance(float2 uv,float d) {
    float water=waterDistance(uv,d);return water>0?water:viewDistance(d);
}
float fogReceiverDistance(float2 uv,float d) {
    // Validate liquid against the actual scene depth. Clamping sky first would
    // reject visible water near the far clip; keep only the dry fallback capped.
    float water=waterDistance(uv,d);return water>0?water:viewDistance(min(d,.99999));
}
float3 affinePoint(float3 p,row_major float4x4 transformMatrix) {
    // Explicit vector rows avoid scalarizing mul(float4,matrix) in Wine's
    // Shader Model 3 compiler while preserving row-vector multiplication.
    return p.x*transformMatrix[0].xyz+p.y*transformMatrix[1].xyz+
           p.z*transformMatrix[2].xyz+transformMatrix[3].xyz;
}
float3 worldPosition(float2 uv,float d) {
    return affinePoint(viewPosition(uv,d),InverseView);
}
float3 surfaceNormal(float2 uv,float d) {
    // Length comparisons commute with the validated orthonormal view rotation.
    // Construct the normal in view coordinates, then rotate it once.
    float3 p=viewPosition(uv,d);
    float2 dx=float2(ImageClip.x,0),dy=float2(0,ImageClip.y);
    float2 lo=.5*ImageClip.xy,hi=1-lo;
    float2 left=max(uv-dx,lo),right=min(uv+dx,hi);
    float2 up=max(uv-dy,lo),below=min(uv+dy,hi);
    float3 l=viewPosition(left,normalizedDepth(left));
    float3 r=viewPosition(right,normalizedDepth(right));
    float3 u=viewPosition(up,normalizedDepth(up));
    float3 b=viewPosition(below,normalizedDepth(below));
    float3 tx=(uv.x<=lo.x||dot(r-p,r-p)<dot(p-l,p-l))&&uv.x<hi.x?r-p:p-l;
    float3 ty=(uv.y<=lo.y||dot(b-p,b-p)<dot(p-u,p-u))&&uv.y<hi.y?b-p:p-u;
    float3 n=cross(tx,ty);n*=rsqrt(max(dot(n,n),1e-12));
    n=dot(n,-p)<0?-n:n;
    return n.x*InverseView[0].xyz+n.y*InverseView[1].xyz+n.z*InverseView[2].xyz;
}

