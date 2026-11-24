// Glow of the nearest authored local lights inside the fog (lanterns, fires):
// in-scattering of an isotropic point source along the view ray in closed form,
// integral of a softened inverse-square source, tapered to zero at its range,
// with the fog extinction sampled at the light. Unshadowed; added to the
// current scattering buffer after atmospheric history/blur. 0.3.205 (gh#20): the batches of four lights add their raw sums into a half-resolution accumulator
// (LocalFog) and LocalFogCombine soft-caps that total once, so the glow does not depend on how the closest-first lights are grouped:
// a per-batch cap made overlapping glows sum to about 1x the cap in one batch and 2x when a lamp swapped into another batch (flicker).
float atanFast(float x){
    // Max error ~0.005 rad, no transcendental instructions.
    float a=abs(x);float r=a<=1?a*(.785398+.273*(1-a)):1.570796-(1/a)*(.785398+.273*(1-1/a));
    return x<0?-r:r;
}
float4 LocalFog(float2 uv:TEXCOORD0):COLOR0 {
    uv=depthUV(uv);
    float depth=normalizedDepth(uv);float3 view=viewPositionDistance(uv,fogReceiverDistance(uv,depth));
    // Rotate the direction without a world-position round trip: nearby rays
    // otherwise quantize as the camera moves at large world coordinates.
    float3 ray=view.x*InverseView[0].xyz+view.y*InverseView[1].xyz+view.z*InverseView[2].xyz;
    float D=min(length(ray),max(FogInfo.w,0));
    ray*=rsqrt(max(dot(ray,ray),1e-12));
    float3 result=0;
    // Four lights per pass to fit SM3. Accumulate radiance across batches;
    // apply the display bound once, without a light-count brightness boost.
    [unroll]for(int i=0;i<4;++i){
        float3 oc=Camera.xyz-LocalLightPos[i].xyz;
        float b=dot(oc,ray);float h2=max(dot(oc,oc)-b*b,0);
        float disc=LocalLightPos[i].w*LocalLightPos[i].w-h2;
        float root=sqrt(max(disc,0));
        float t0=max(-b-root,FogRange.x),t1=min(-b+root,D);
        [branch]if(disc>0&&t1>t0){
        // Finite source core removes the sharp pin of light. Subtracting the
        // boundary value makes volume density vanish continuously at the
        // sphere edge, instead of producing a bright, hard-edged ball.
        float core=max(.75,LocalLightPos[i].w*.06);
        float invH=rsqrt(h2+core*core);
        float boundary=rcp(max(LocalLightPos[i].w*LocalLightPos[i].w+core*core,.001));
        float integral=max((atanFast((t1+b)*invH)-atanFast((t0+b)*invH))*invH-(t1-t0)*boundary,0);
        // 0.3.199 (fog clouds): the same smooth near ramp as WorldFog's air (0 at FogRange.x, full one fade length later), taken at the
        // ray's closest approach to the light inside the integrated span. The glow's bright core sits there; with only the hard FogRange.x
        // start it was cut off within a unit or two of walking, so the glow popped while approaching a lamp (game test).
        float nearGlow=saturate((clamp(-b,t0,t1)-FogRange.x)*FogRange.y);nearGlow*=nearGlow*(3-2*nearGlow);
        float3 lamp=LocalLightColor[i].rgb*(integral*LocalLightFog[i].x*nearGlow);
        result+=lamp;
        }
    }
    return float4(result*FogRange.z,0);
}
// One hue-preserving bound over all lamps, independent of their batch order.
float4 LocalFogCombine(float2 uv:TEXCOORD0):COLOR0 {
    float4 acc=tex2Dlod(FogBuffer,float4(uv,0,0));
    float3 scatter=acc.rgb;
    float peak=max(scatter.r,max(scatter.g,scatter.b));
    scatter*=FogRange.w/(FogRange.w+peak);
    return float4(scatter,0);
}
float4 upsampleFog(float2 uv,float centerDepth) {
    // Sample actual half-resolution texel centers. This also handles odd full
    // viewport dimensions such as 1728x1117 -> 864x558 without a half-row drift.
    float2 dimensions=ScreenSize.zw;
    float2 grid=uv*dimensions-.5,base=floor(grid),fraction=grid-base;
    float center=receiverDistance(depthUV(uv),centerDepth),total=0,closest=1e20;
    float4 sum=0,fallback=float4(0,0,0,1);
    [loop]for(int i=0;i<4;++i){
        float row=floor(i*.5);
        float2 bit=float2(i-row*2,row);
        float2 q=(clamp(base+bit,0,dimensions-1)+.5)/dimensions;
        float delta=abs(receiverDistance(depthUV(q),normalizedDepth(depthUV(q)))-center);
        float4 sample=tex2Dlod(FogBuffer,float4(q,0,0));
        float2 blend=lerp(1-fraction,fraction,bit);
        float weight=blend.x*blend.y*exp(-delta/max(.2,center*.02));
        sum+=sample*weight;total+=weight;
        if(delta<closest){closest=delta;fallback=sample;}
    }
    return total>.00001?sum*(1/total):fallback;
}
// Depth-aware separable blur of the half-resolution scattering buffer. Canopy
// gaps in the 0.09-unit shadow texels produce shafts one texel wide, which read
// as rain; two 5-tap passes with a 2-texel stride turn them into soft beams.
// Receiver depth keeps a near trunk's short path from bleeding into the sky.
float blurReceiverDistance(float2 uv) {
    // TemporalLight wrote this exact half-resolution centre's dry distance.
    // Water still validates against the original scene distance, not a sky cap.
    float z=tex2Dlod(CurrentDistance,float4(uv,0,0)).r;
    if(WaterInfo.x>=.5){
        float2 water=tex2Dlod(WaterMask,float4(depthUV(uv),0,0)).rg;
        if(water.x>ImageClip.z&&water.x<=z+max(.012,water.x*.0007)&&water.y>.003)return water.x;
    }
    return z;
}
float4 FogBlur(float2 uv:TEXCOORD0):COLOR0 {
    float2 dimensions=ScreenSize.zw;
    float2 base=floor(uv*dimensions);
    float2 centerUV=(base+.5)/dimensions;
    float center=blurReceiverDistance(centerUV);
    float4 sum=0;float total=0;
    [unroll]for(int i=-2;i<=2;++i){
        float2 q=(clamp(base+BlurInfo.xy*i,0,dimensions-1)+.5)/dimensions;
        float z=i==0?center:blurReceiverDistance(q);
        float w=(i==0?6:(abs(i)==1?4:1))*exp(-abs(z-center)/max(.5,center*.06));
        sum+=tex2Dlod(FogBuffer,float4(q,0,0))*w;total+=w;
    }
    return sum/max(total,1e-5);
}
// 0.3.199 (fog clouds): moving low fog banks. A second half-resolution raymarch of the same world-fixed intervals as WorldFog (40, no jitter:
// fog has no temporal history) through two scales of tileable 3D noise scrolled by the wind (origins in c60/c61). Returns the cloud
// scattering (rgb) and the cloud transmittance (a); drawn with ONE, SRCALPHA so it adds to the fog buffer and attenuates what is there.
// Exactly (0,0,0,1) where no cloud is met. The base fog only attenuates the cloud light (Tbase, never written to alpha); its density is a cut-down
// copy of WorldFog's (the full lines do not fit the 512-slot budget next to the field fetch and the shadow filter), see the comments below.
float4 fogClouds(float2 uv,bool actorIntegration,bool tiledActor=false,bool directional=true) {
    // (no FogInfo.x test: the host draws this pass only when c21.x >= .5)
    uv=depthUV(uv);
    float depth=normalizedDepth(uv);float3 view=viewPositionDistance(uv,fogReceiverDistance(uv,depth));
    float3 ray=view.x*InverseView[0].xyz+view.y*InverseView[1].xyz+view.z*InverseView[2].xyz;
    float surface=min(length(ray),FogInfo.w);
    [branch]if(surface<=max(FogRange.x,0))return float4(0,0,0,1);
    ray*=rsqrt(max(dot(ray,ray),1e-12));
    float3 shadowOrigin=affinePoint(Camera.xyz,FarMatrix);
    float3 shadowRay=ray.x*FarMatrix[0].xyz+ray.y*FarMatrix[1].xyz+ray.z*FarMatrix[2].xyz;
    float3 actorOrigin=affinePoint(Camera.xyz,FogActorMatrix);
    float3 actorRay=ray.x*FogActorMatrix[0].xyz+ray.y*FogActorMatrix[1].xyz+ray.z*FogActorMatrix[2].xyz;
    float tauBase=0,tCloud=1,directWeight=0,ambientWeight=0; // scalar sums: the sun and sky colours multiply once after the loop; the base fog as optical depth (exp only inside a cloud)
    float3 axis=abs(ray);
    float2 major=axis.x>=axis.y?float2(ray.x,Camera.x):float2(ray.y,Camera.y);
    major=abs(major.x)>=axis.z?major:float2(ray.z,Camera.z);
    float spacing=mad(FogInfo.w,1.0/40,CloudInfo[0].x); // 0.3.200 (gpu budget): c60.x 0 = the 40 intervals; reduced levels fewer (host: 128*(1/N-1/40))
    float stepLength=spacing/abs(major.x);
    float offset=frac(major.y*(major.x<0?-1:1)/spacing);
    // Skip whole world-grid intervals beyond the regional field support.
    float2 heights=FogHeightControl.z>0?FogHeightControl.xy:float2(-1e20,1e20);
    float3 invRay=(ray<0?-1:1)/max(abs(ray),1e-8);
    float3 fieldA=(float3(RegionalFogInfo.xy,heights.x)-Camera.xyz)*invRay;
    float3 fieldB=(float3(RegionalFogInfo.xy+63.0/(64*max(RegionalFogInfo.z,1e-8)),heights.y)-Camera.xyz)*invRay;
    float3 entry=min(fieldA,fieldB),exit=max(fieldA,fieldB);
    // Keep the prefix before entering the cloud layer: its base-fog optical
    // depth attenuates later clouds. Only intervals beyond the last possible
    // bank can be omitted; their accumulated base density is never consumed.
    float fieldEntry=max(0,max(entry.x,entry.y)),fieldExit=min(exit.x,min(exit.y,exit.z));
    if(fieldExit<fieldEntry||entry.z>surface)return float4(0,0,0,1);
    float marchEnd=min(surface,fieldExit+stepLength);
    int first=max(0,(int)floor(max(max(FogRange.x,0),fieldEntry-stepLength)/stepLength+offset));
    [loop]for(int i=first;i<41&&max(0,(i-offset)*stepLength)<marchEnd;++i){
        float start=max(0,(i-offset)*stepLength);
        float end=min((i+1-offset)*stepLength,surface);
        float stepSize=end-start;
        float mid=(start+end)*.5;
        float3 rel=ray*mid;
        float3 p=Camera.xyz+rel;
        float2 fieldUV=(p.xy-RegionalFogInfo.xy)*RegionalFogInfo.z+.5/64;
        float coverage;
        float4 field=regionalFogAt(fieldUV,coverage);
        float altitude=p.z-field.x;
        float valid=altitude>=0?coverage:0;
        // Invalid samples contribute exactly zero; retain the world-grid intervals.
        [branch]if(valid>0){
            float nearFade=saturate((mid-FogRange.x)*FogRange.y);nearFade*=nearFade*(3-2*nearFade);
            // 0.3.199 (fog clouds): the ground term of WorldFog, copied exactly (thick Duskwood/STV ground fog must hide the clouds behind it).
            float profile=saturate((field.w-2.5)/2.5);
            float groundHeight=lerp(field.w,6,RegionalFogInfo.w*(1-profile));
            float vertical=saturate(1-altitude/max(groundHeight,.001));
            float groundSigma=max(field.y+field.z*RegionalFogInfo.w,0)*vertical*vertical;
            // Air term reduced to its constant floor for the slot budget (no vertical profile, no forest policy).
            float airSigma=WeatherInfo.w;
            float baseSigma=max(groundSigma+airSigma,0)*valid*nearFade;
            // 0.3.199 (fog clouds, optimisation): the noise only where a bank can be: inside the field above ground, below the tallest bank top
            // (c62.x = 5.5 x height, host), and the small noise only where the large one can still give density (c61.x, host: below it either the
            // bank top or the density is 0 whatever the small noise). Skipped samples had sigma 0.
            float sigma=0;
            [branch]if(min(valid,CloudInfo[2].x-altitude)>0){ // valid > 0 implies altitude >= 0
                float nL=tex3Dlod(CloudNoise,float4(rel*CloudInfo[3].y+CloudInfo[0].yzw,0)).r;
                [branch]if(nL>CloudInfo[1].x){
                    float low=saturate(1-altitude/max(mad(nL,CloudInfo[2].z,WeatherInfo.y),.001)); // height x (8 nL - 2.5), folded on the host (c62.z 8 height, c59.y -2.5 height) // 0.3.199 (fog clouds): the bank top follows the large noise (0 below nL .31, ~1.5x at .5, ~4x at .8, 5.5x at 1): thick spots tower over the trees (game tests)
                    // Above the noise-dependent bank top, the small noise contributes exactly zero.
                    [branch]if(low>0){
                    float nS=tex3Dlod(CloudNoise,float4(rel*CloudInfo[3].z+CloudInfo[1].yzw,0)).r;
                    float density=saturate(mad(nL,CloudInfo[3].w,mad(nS,CloudInfo[3].x,CloudInfo[2].y))); // (.65 nL + .35 nS - threshold) x sharpness, folded on the host
                    float zone=mad(saturate(mad(field.w,1/3.75,-1.25/3.75)),-.3,1); // lerp(1,.7,saturate((field.w-1.25)/3.75))
                    sigma=density*low*low*zone*CloudInfo[2].w*valid*nearFade; // valid > 0 only above ground (altitude >= 0) in a node with a layer height tag
                    }
                }
            }
            [branch]if(sigma>0){
                float absorb=1-exp(-sigma*stepSize);
                float heightFade=saturate(altitude*(1.0/12));heightFade*=heightFade*(3-2*heightFade);
                float weight=tCloud*absorb; // the ambient: the pass blend already multiplies the base fog by tCloud, tBase must not enter twice (S*(1-Tb*Tc) in either order)
                // With no direct source/gain the shadowed term is exactly zero; ambient extinction still accumulates.
                [branch]if(directional&&dot(max(DirectLight.rgb,0),1)>0&&FogInfo.y>0){
                    float visibility=fogShadowProjected(shadowOrigin+shadowRay*((start+end)*.5));
                    [branch]if(actorIntegration&&FogActorInfo.x>0&&visibility>0)visibility=fogCombineActor(visibility,tiledActor?fogActorShadowTiled(actorOrigin+actorRay*start,actorRay*stepSize):fogActorShadow(actorOrigin+actorRay*start,actorRay*stepSize));
                    directWeight+=exp(-tauBase)*weight*visibility*lerp(1,heightFade,SunDirection.w);
                } // the sun light is dimmed by the base fog in front of the cloud
                ambientWeight+=weight;
                tCloud*=1-absorb;
            }
            tauBase+=baseSigma*stepSize;
        }
    }
    // The narrow forward aureole of WorldFog is dropped here (register/slot budget): the broad phase alone.
    float3 directScatter=directional?max(DirectLight.rgb,0)*mad(.033,dot(ray,SunDirection.xyz),.22)*(max(FogInfo.y,0)*directWeight):0; // FogColor.rgb (the albedo) is always 1 (host): left out for the slots
    float peak=max(directScatter.r,max(directScatter.g,directScatter.b));
    float cap=max(FogInfo.z,.0001);
    directScatter*=cap/(cap+peak);
    // 0.3.199 (fog clouds): the banks take the brighter of the host's cloud colour (c26/c25.w for this pass only: the game's fog colour, the colour
    // the distant world already fades to, raised to a moonlit grey at night; NorthlightFogClouds::colour)
    // and WorldFog's ambient*.35 air radiance, per channel; the storm bands make the game colour grey in rain. By day in rain the air radiance is near
    // black under the storm sky and the game colour wins; at night the game colour is near black while the air WorldFog lit around it
    // is brighter, and a bank that hides that air behind a darker colour reads as a black smear (game tests). The host always sets c25.w 1 here
    // (without a validated game fog its colour is the grey floor alone); FogColor.rgb, the scattering albedo, is always 1: left out for the slots.
    return float4(max(max(AmbientLight.rgb,0)*.35,LegacyFogColor.rgb*LegacyFog.w)*ambientWeight+directScatter,tCloud);
}
// 0.3.199 (fog temporal): temporal accumulation of the half-resolution fog (raw FogBuffer, s9) with last frame's resolved fog (FogHistory, s14
// LINEAR), reprojected through the previous view (c53..c56, still the previous frame's here) and accepted only where the previous frame's
// stored view distance (DepthHistory, s15) agrees; sky pixels reproject their view direction only. History is clamped to the current 3x3
// neighbourhood (all four channels) so a moving lamp or shaft leaves no trail, then lerped with the current by c64.y (0 = pass through).
sampler2D FogHistory : register(s14);
float4 FogTemporalInfo : register(c64); // y history weight (c64.yzw are read by no other shader: LocalFog reads only .x of c59..c66)
float4 FogTemporal(float2 uv:TEXCOORD0):COLOR0 {
    float2 half=ScreenSize.zw;
    float2 base=floor(uv*half);float2 q=(base+.5)/half;
    float4 current=tex2Dlod(FogBuffer,float4(q,0,0));
    if(FogTemporalInfo.y<=0)return current;
    float2 duv=depthUV(q);float d=normalizedDepth(duv);bool sky=d>=.99999;
    float3 v;
    if(sky){
        float3 r=viewPositionDistance(duv,1);
        float3 world=r.x*InverseView[0].xyz+r.y*InverseView[1].xyz+r.z*InverseView[2].xyz;
        v=world.x*PreviousView[0].xyz+world.y*PreviousView[1].xyz+world.z*PreviousView[2].xyz;
    }else v=affinePoint(worldPosition(duv,d),PreviousView);
    float w=v.z*Projection.z;
    if(w<=ImageClip.z)return current;
    float2 puv=(v.xy*Projection.xy/w)*float2(.5,-.5)+.5;
    if(any(puv<0)||any(puv>1))return current;
    float2 pq=(clamp(floor(puv*half),0,half-1)+.5)/half;
    float2 texel=1/half,corner=(floor(puv*half-.5)+.5)*texel;
    // Sky reprojection has no finite surface: use a validated point history
    // sample so a former foreground neighbour cannot enter through bilinear fog.
    float4 history=tex2Dlod(FogHistory,float4(sky?pq:puv,0,0));
    [branch]if(all(history==current)&&all(current!=0)&&all(abs(current)<1e20))return current;
    if(sky){
        if(tex2Dlod(DepthHistory,float4(pq,0,0)).r<viewDistance(.99999))return current;
    }else{
        float4 footprint=float4(tex2Dlod(DepthHistory,float4(corner,0,0)).r,
            tex2Dlod(DepthHistory,float4(corner+float2(texel.x,0),0,0)).r,
            tex2Dlod(DepthHistory,float4(corner+float2(0,texel.y),0,0)).r,
            tex2Dlod(DepthHistory,float4(corner+texel,0,0)).r);
        float4 error=abs(footprint-w)-max(.25,w*.03);
        float2 nearest=floor(puv*half)-floor(puv*half-.5);
        float pointError=lerp(lerp(error.x,error.y,nearest.x),lerp(error.z,error.w,nearest.x),nearest.y);
        if(pointError>0)return current;
        [branch]if(any(error>0))history=tex2Dlod(FogHistory,float4(pq,0,0));
    }
    // Current is inside its own clamp window. Keep lerp's exact arithmetic,
    // including signed zero/nonfinite behavior, when the samples already agree.
    [branch]if(all(history==current))return lerp(current,history,FogTemporalInfo.y);
    float4 lo=current,hi=current;
    [unroll]for(int i=0;i<8;++i){
        float2 offset=float2(i%3-1,i/3-1);
        offset=i>=4?float2((i+1)%3-1,(i+1)/3-1):offset;
        float4 s=tex2Dlod(FogBuffer,float4((clamp(base+offset,0,half-1)+.5)/half,0,0));
        lo=min(lo,s);hi=max(hi,s);
    }
    return lerp(current,clamp(history,lo,hi),FogTemporalInfo.y);
}
