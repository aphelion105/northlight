// Draw after the native opaque world, before AO's scene copy and the extension
// world/fog composite, so atmosphere layers over the disc; always before UI.
// Core blend SRCALPHA/INVSRCALPHA; glare (and veil) blend INVDESTCOLOR/ONE:
// a screen blend of 1-exp(-glow), so the glow never clips to a plateau.
// CelestialVeilPS draws later (after the world composite and water, before UI).
// Samples only clear world-depth pixels; native
// opaque geometry and visible water occlude the celestial surface.
sampler2D DiscDepth : register(s0);
sampler2D DiscTexture : register(s1);
sampler2D DiscWater : register(s2);
sampler2D DiscHaloVisibility : register(s3); // current halo, previous 1x1 value in visibility pass
sampler2D DiscTerrain : register(s4); // viewer-ray coverage of already loaded terrain
sampler2D DiscRingVisibility : register(s5); // 1x1 visibility of the 2-4R ring (sun), else the halo value
float4 DiscTerrainPolicy : register(c46); // ready, mask extent in body radii
float4 DiscImage : register(c0); // inv fullWidth/Height, world depthMin, inverse depthRange
float4 DiscProjection : register(c1); // signed X,Y,Zsign, waterReady
row_major float4x4 DiscInverseView : register(c2);
float4 DiscDirection : register(c6); // directionXYZ, tangent angular radius
float4 DiscRight : register(c7);
float4 DiscUp : register(c8);
float4 DiscColor : register(c9); // original encoded texture tint RGB, opacity*horizonfade
float4 DiscPolicy : register(c10); // outputLinear (SRGBWRITE enabled), near,far, occlusion enabled (0 for the sky-phase draw)
float4 DiscRepair : register(c11); // early raw depth, repair enabled, D24 tolerance, analytic sun alpha
float4 DiscEmission : register(c12); // disc: core gain; glare: hot-core rate; glare radius in disc radii, glare core weight, glare pass
float4 DiscSourceUV[32] : register(c13); // fixed equal-area taps: xy scene UV (-1 invalid), zw terrain mask UV
float4 DiscHistory : register(c45); // previous valid, falling/rising update factors
float4 DiscGlowHue : register(c47); // glow hue RGB (zone/time sun glow colour); disc: hue mix, glare: tail weight
float4 DiscGlowCore : register(c48); // hot-core colour RGB, ring wrap (0 = no ring)
float3 discRay(float2 uv){
    float2 ndc=(uv-.5*DiscImage.xy)*float2(2,-2)+float2(-1,1);
    float3 view=float3(ndc/DiscProjection.xy,DiscProjection.z);
    return view.x*DiscInverseView[0].xyz+view.y*DiscInverseView[1].xyz+view.z*DiscInverseView[2].xyz;
}
float terrainVisibility(float3 ray){
    if(DiscTerrainPolicy.x<.5)return 1;
    float forward=dot(ray,DiscDirection.xyz);
    if(forward<=0)return 1;
    float2 local=float2(dot(ray,DiscRight.xyz),-dot(ray,DiscUp.xyz))/(forward*DiscDirection.w*DiscTerrainPolicy.y);
    if(max(abs(local.x),abs(local.y))>1)return 1;
    return 1-saturate(tex2Dlod(DiscTerrain,float4(local*.5+.5,0,0)).r);
}
float skyVisibility(float4 source) {
    float2 uv=source.xy;
    float rawDepth=tex2Dlod(DiscDepth,float4(uv,0,0)).r;
    float depth=saturate((rawDepth-DiscImage.z)*DiscImage.w);
    float visible=(source.z>=.5&&depth>=.99999994)?1:0;
    if(DiscRepair.y>.5&&rawDepth<DiscRepair.x-DiscRepair.z)visible=0;
    if(DiscProjection.w>.5){float2 water=tex2Dlod(DiscWater,float4(uv,0,0)).rg;
        visible*=1-((water.x>DiscPolicy.y&&water.x<=DiscPolicy.z&&water.y>.003)?1:0);}
    return visible;
}
// Filter binary visibility, never interpolated depth: depth interpolation at a
// tree silhouette can invent occluders. This also smooths subpixel camera moves.
float filteredSkyVisibility(float4 source){
    float2 dimensions=floor(1/DiscImage.xy+.5);
    float2 grid=source.xy*dimensions-.5,base=floor(grid),f=frac(grid);
    float4 a=float4((base+.5)*DiscImage.xy,source.x>=0?1:0,0);
    float4 b=a;b.x+=DiscImage.x;float4 c=a;c.y+=DiscImage.y;float4 d=b;d.y+=DiscImage.y;
    return lerp(lerp(skyVisibility(a),skyVisibility(b),f.x),lerp(skyVisibility(c),skyVisibility(d),f.x),f.y);
}
// Glare profile in disc radii. Solar: core, middle glow, atmospheric tail and
// a broad term that carries the glow to ~20R; all decay monotonically and the
// edge fades them to zero at the support. Lunar: exponential skirt from the rim.
// Glare "optical depth" x in disc radii (the output is 1-exp(-x), so no
// plateau or clipping rim). Solar: a small hot core (weight c12.z, rate c12.x,
// ~1-1.5R) plus a long soft tail (weight c47.w) to the 20R support. Lunar: the
// M1 skirt .35*2^(-1.11(r-1)) times c12.z. The edge fades both to 0 at the support.
// Occlusion (r47): the hot core is the disc itself and uses the disc taps'
// visibility only, so it is never drawn over geometry that hides the disc (a
// near mountain). The 2-4R ring (x wrap) carries only a core-free tail: the
// tail shifted out by 3R, tail(sqrt(r^2+9)), flat-topped at the tail's 3R
// level. Both parts fall monotonically with radius, so a barely hidden sun
// (small disc, bright ring) shows no dark ring around it.
float glareProfile(float radius){
    float r2=radius*radius;
    float edge=1-smoothstep(.35*DiscEmission.y,DiscEmission.y,radius);
    float disc=saturate(tex2Dlod(DiscHaloVisibility,float4(.5,.5,0,0)).r);
    float ring=DiscGlowCore.w*saturate(tex2Dlod(DiscRingVisibility,float4(.5,.5,0,0)).r);
    float3 tail=float3(exp2(-.12*r2),exp2(-.025*r2),exp2(-.008*r2));
    // tail(sqrt(r^2+9)) = sum w_i*2^(-k_i*9)*2^(-k_i*r^2): the same three terms.
    float tailed=max(disc*dot(tail,float3(.25,.35,.40)),ring*dot(tail,float3(.118257,.299458,.380527)));
    if(DiscRepair.w>.5)return (DiscEmission.z*exp2(-DiscEmission.x*r2)*disc+DiscGlowHue.w*tailed)*edge;
    return DiscEmission.z*.35*exp2(-1.11*max(radius-1,0))*edge*disc;
}
// Tinted by the glow hue; inside the hot core it goes to the core colour, so a
// saturated sunset hue turns warm white instead of a red core. Mirrors
// NorthlightSunHue::glowColour (lerp(sun,core,smoothstep(inner))) with inner the
// core's own falloff; the hue mix with today's tint is folded in on the CPU.
float3 glowColor(float radius){
    return saturate(lerp(DiscGlowHue.rgb,DiscGlowCore.rgb,smoothstep(0,1,exp2(-DiscEmission.x*radius*radius))));
}
// Soft shoulder: 1-exp(-x*colour), written for the INVDESTCOLOR/ONE screen
// blend: dst' = 1-(1-dst)*exp(-x*colour). Monotonic and smooth; never clips.
float3 glowShoulder(float x,float3 color){return 1-exp2(-1.442695*x*color);}
float3 outputColor(float3 encoded){
    // Texture sampling is raw (sRGB sampler OFF). The current renderer normally
    // writes encoded framebuffer values directly; if caller enables SRGBWRITE,
    // explicitly decode exactly once before the hardware encoder.
    float3 linearColor=encoded<=.04045?encoded/12.92:pow((encoded+.055)/1.055,2.4);
    return DiscPolicy.x>.5?linearColor:encoded;
}
float4 CelestialHaloVisibilityPS(float2 uv:TEXCOORD0):COLOR0 {
    float current=0;
    [loop]for(int i=0;i<32;++i){
        float4 source=DiscSourceUV[i];
        float terrain=DiscTerrainPolicy.x>.5?1-saturate(tex2Dlod(DiscTerrain,float4(source.zw,0,0)).r):1;
        current+=filteredSkyVisibility(source)*terrain;
    }
    current*=1.0/32;
    // Retain more halo around a partially covered source: half-visible yields
    // 75% instead of 50%. Endpoints stay exact; a full blocker still hides it.
    // Apply before temporal filtering so the existing full-occlusion fade holds.
    current=current*(2-current);
    // Do not read uninitialized ping-pong storage on first use or a reset.
    float value=current;
    if(DiscHistory.x>.5){
        float previous=saturate(tex2Dlod(DiscHaloVisibility,float4(.5,.5,0,0)).r);
        value=lerp(previous,current,current<previous?DiscHistory.y:DiscHistory.z);
    }
    // Make sustained full occlusion exactly zero after the short fade.
    if(current<=0&&value<.001)value=0;
    return saturate(value).xxxx;
}
float4 CelestialDiscPS(float2 uv:TEXCOORD0):COLOR0 {
    float2 dimensions=floor(1/DiscImage.xy+.5);
    float2 depthUV=(clamp(floor(uv*dimensions),0,dimensions-1)+.5)*DiscImage.xy;
    float rawDepth=tex2Dlod(DiscDepth,float4(depthUV,0,0)).r;
    float depth=saturate((rawDepth-DiscImage.z)*DiscImage.w);
    clip((1-DiscRepair.y)+DiscRepair.y*(rawDepth-DiscRepair.x+DiscRepair.z));
    // One normalized 24-bit depth quantization step. Do not draw over nearby
    // roofs, terrain silhouettes or actors merely because sky color is similar.
    // Strict depth occlusion, like the native billboard: every surface in the
    // depth buffer, fogged or not, hides the disc. (A fog-based exception was
    // tried in 0.3.25 and drew the moon over distant fogged trees.)
    if(DiscPolicy.w>.5&&depth<.99999994)return 0;
    if(DiscPolicy.w>.5&&DiscProjection.w>.5){float2 water=tex2Dlod(DiscWater,float4(depthUV,0,0)).rg;
        if(water.x>DiscPolicy.y&&water.x<=DiscPolicy.z&&water.y>.003)return 0;}
    float3 ray=discRay(uv);
    float terrainVisible=terrainVisibility(ray);
    // Apply to the ORIGINAL sky-phase draw too: a late-only mask cannot erase
    // a disc already painted over a fogged/WDL silhouette without native depth.
    if(DiscPolicy.w<.5)clip(terrainVisible-.99999);
    clip(terrainVisible-.001);
    float forward=dot(ray,DiscDirection.xyz);clip(forward-.000001);
    float2 local=float2(dot(ray,DiscRight.xyz),-dot(ray,DiscUp.xyz))/(forward*DiscDirection.w);
    float4 texel=tex2Dlod(DiscTexture,float4(local*.5+.5,0,0));
    // suncenter.blp has only 16 alpha levels. Its magnified rim and the
    // complementary glare mask showed those steps as sharp concentric edges.
    // r43: a soft edge (opaque core ~0.44 radius) that fades continuously to 1,
    // so the glare washes the disc out when facing the sun.
    // The moon retains its authored silhouette and surface detail.
    if(DiscRepair.w>.5){float a=saturate((1-length(local))/.56);texel.a=a*a*(3-2*a);}
    // Only opaque celestial pixels may reserve sky depth. A translucent rim that
    // writes depth blocks later sky/cloud color and leaves a dark outline.
    // The late depth-checked repair draws the soft rim over the finished sky.
    if(DiscPolicy.w<.5)clip(texel.a-.99999);
    float3 encoded;float alpha;
    if(DiscEmission.w>.5){
        float radius=length(local);
        clip(DiscEmission.y-radius);
        // Visibility of the source itself is independent of the output pixel:
        // a fully hidden sun (disc and ring) cannot leak glow around a roof.
        encoded=glowColor(radius);
        alpha=saturate(DiscColor.w)*glareProfile(radius);
        // Preserve lunar craters/tint; solar glare also fills its center.
        if(DiscRepair.w<.5)alpha*=1-texel.a;
    }else{
    clip(1-max(abs(local.x),abs(local.y)));
    // Explicit LOD is valid after divergent silhouette/depth branches. Assets
    // are decoded at <=256 pixels; the caller can supply their original mips.
    // Limit brightness before tinting so bright cores retain the regional hue.
    encoded=saturate(texel.rgb*DiscEmission.x)*lerp(saturate(DiscColor.rgb),DiscGlowCore.rgb,.5*DiscGlowHue.w);
    alpha=texel.a*saturate(DiscColor.w);
    }
    alpha*=terrainVisible;
    // In the early sky pass an invisible quad pixel must not write sky depth.
    clip(alpha-.001);
    if(DiscEmission.w>.5)encoded=glowShoulder(alpha,encoded);
    return float4(outputColor(encoded),alpha);
}
// Veil over geometry (sun): the complement of the sky glare's own coverage,
// drawn into the target water just used. Geometry is classified by RAW depth
// against the sky-band sprite depth, so WDL and beyond-far silhouettes, which
// the glare's repair test removes from the sky, are veiled too; visible water
// and loaded terrain behind the sky likewise. Blend INVDESTCOLOR/ONE like the glare,
// so the colour continues across silhouettes. c12.z and c47.w already hold the
// glare weights x veil strength x (1 - horizon haze at the sun) x fades.
float4 CelestialVeilPS(float2 uv:TEXCOORD0):COLOR0 {
    float2 dimensions=floor(1/DiscImage.xy+.5);
    float2 depthUV=(clamp(floor(uv*dimensions),0,dimensions-1)+.5)*DiscImage.xy;
    float rawDepth=tex2Dlod(DiscDepth,float4(depthUV,0,0)).r;
    float depth=saturate((rawDepth-DiscImage.z)*DiscImage.w);
    float sky=(depth>=.99999994&&rawDepth>=DiscRepair.x-DiscRepair.z)?1:0;
    if(DiscProjection.w>.5){float2 water=tex2Dlod(DiscWater,float4(depthUV,0,0)).rg;
        if(water.x>DiscPolicy.y&&water.x<=DiscPolicy.z&&water.y>.003)sky=0;}
    float3 ray=discRay(uv);
    float forward=dot(ray,DiscDirection.xyz);clip(forward-.000001);
    float geometry=1-sky*terrainVisibility(ray);
    float2 local=float2(dot(ray,DiscRight.xyz),-dot(ray,DiscUp.xyz))/(forward*DiscDirection.w);
    float radius=length(local);
    float alpha=geometry*glareProfile(radius);
    clip(alpha-.001);
    return float4(outputColor(glowShoulder(alpha,glowColor(radius))),alpha);
}

row_major float4x4 TerrainMaskMatrix : register(c0);
float4 CelestialTerrainVS(float3 position:POSITION0):POSITION0 {
    return mul(float4(position,1),TerrainMaskMatrix);
}
float4 CelestialTerrainPS():COLOR0 {return 1;}
