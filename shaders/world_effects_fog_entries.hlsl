// Exact actor interval integration needs extended SM3 instruction capacity.
// Baseline entries retain the far-map path on minimum-capability hardware.
float4 WorldFog(float2 uv:TEXCOORD0):COLOR0{return worldFog(uv,false);}
float4 WorldFogAmbient(float2 uv:TEXCOORD0):COLOR0{return worldFog(uv,false,false);}
float4 WorldFogActor(float2 uv:TEXCOORD0):COLOR0{return worldFog(uv,true);}
float4 WorldFogActorTiled(float2 uv:TEXCOORD0):COLOR0{return worldFog(uv,true,true,true);}
float4 FogCloudsAmbient(float2 uv:TEXCOORD0):COLOR0{return fogClouds(uv,false,false,false);}
float4 FogClouds(float2 uv:TEXCOORD0):COLOR0{return fogClouds(uv,false);}
float4 FogCloudsActor(float2 uv:TEXCOORD0):COLOR0{return fogClouds(uv,true);}
float4 FogCloudsActorTiled(float2 uv:TEXCOORD0):COLOR0{return fogClouds(uv,true,true);}

#ifndef NORTHLIGHT_SHADOW_TRANSITION
bool fogDirectContained(float3 q){
    return all(abs(q.xy)<1-1.5/DepthParams.z-DepthParams.w)&&q.z>PassInfo.x*4&&q.z<1;
}
#else
bool fogDirectContained(float3 q);
#endif
sampler2D DirectFogFloor : register(s0);
#define DirectFogSupport FogActorInfo // yzw: floor ready, shadow current, field matches
float4 worldMoonFloorFog(float2 uv,bool actorIntegration,bool directIntegration=true,bool tiledActor=false) {
    if(FogInfo.x<.5)return float4(0,0,0,1);
    uv=depthUV(uv);
    float depth=normalizedDepth(uv);float3 view=viewPositionDistance(uv,fogReceiverDistance(uv,depth));
    // Rotate a direction directly: adding then subtracting a large camera
    // translation loses precision as the player moves through world space.
    float3 ray=view.x*InverseView[0].xyz+view.y*InverseView[1].xyz+view.z*InverseView[2].xyz;
    float surface=min(length(ray),FogInfo.w); // host supplies the positive 128-unit range
    // All interval midpoints are before the authored near fade: no extinction.
    [branch]if(surface<=max(FogRange.x,0))return float4(0,0,0,1);
    ray*=rsqrt(max(dot(ray,ray),1e-12));
    float3 shadowOrigin=affinePoint(Camera.xyz,FarMatrix);
    float3 shadowRay=ray.x*FarMatrix[0].xyz+ray.y*FarMatrix[1].xyz+ray.z*FarMatrix[2].xyz;
    float3 actorOrigin=affinePoint(Camera.xyz,FogActorMatrix);
    float3 actorRay=ray.x*FogActorMatrix[0].xyz+ray.y*FogActorMatrix[1].xyz+ray.z*FogActorMatrix[2].xyz;
    bool floorReady=directIntegration&&SunDirection.w>.5&&all(DirectFogSupport.yzw>.5);
    float transmittance=1,directTransmission=1,directWeight=0;
    float cosine=dot(ray,SunDirection.xyz); // both directions are normalized
    // Normalized HG(g=.85) + isotropic mixture: most energy is broad enough
    // for side/back views of geometry-cut shafts; a small forward aureole remains.
    float narrow=.2775*pow(max(1.7225-1.7*cosine,.01),-1.5);
    // The phase describes the air, independent of screen-space disc occlusion.
    // Each sample's shadow ray determines illumination; receiver depth ends
    // the march at foreground geometry, including the player.
    // 0.3.145: the broad part leans gently toward the source (x1.15 facing it,
    // x1 at 90 degrees, x.85 facing away); its sphere integral is unchanged.
    float phasePi=mad(.03,narrow,mad(.033,cosine,.22));
    float3 sunSource=directIntegration?max(DirectLight.rgb,0)*phasePi*saturate(FogColor.rgb)*max(FogInfo.y,0):0;
    // Intervals are cut by fixed world-axis planes, not concentric shells
    // travelling with the camera. Use the dominant ray axis to avoid grazing
    // steps; clip both end intervals so their optical path length stays exact.
    float3 axis=abs(ray);
    float2 major=axis.x>=axis.y?float2(ray.x,Camera.x):float2(ray.y,Camera.y);
    major=abs(major.x)>=axis.z?major:float2(ray.z,Camera.z);
    float spacing=mad(FogInfo.w,1.0/48,FogStepInfo.z); // 0.3.200 (gpu budget): c64.z 0 = the 48 intervals; reduced levels fewer (host: 128*(1/N-1/48))
    float stepLength=spacing/abs(major.x);
    float offset=frac(major.y*(major.x<0?-1:1)/spacing);
    // Put the exit comparison on the loop: native SM3 compilation of a nested
    // bare break emitted break_ne with identical operands, marching past surface.
    // Intervals ending before the near fade have exactly zero extinction.
    // Skip whole world-grid intervals beyond the regional field support.
    float2 heights=FogHeightControl.z>0?FogHeightControl.xy:float2(-1e20,1e20);
    float3 invRay=(ray<0?-1:1)/max(abs(ray),1e-8);
    float3 fieldA=(float3(RegionalFogInfo.xy,heights.x)-Camera.xyz)*invRay;
    float3 fieldB=(float3(RegionalFogInfo.xy+63.0/(64*max(RegionalFogInfo.z,1e-8)),heights.y)-Camera.xyz)*invRay;
    float3 entry=min(fieldA,fieldB),exit=max(fieldA,fieldB);
    float fieldEntry=max(0,max(entry.x,max(entry.y,entry.z))),fieldExit=min(exit.x,min(exit.y,exit.z));
    if(fieldExit<fieldEntry)return float4(0,0,0,1);
    float marchEnd=min(surface,fieldExit+stepLength);
    int first=max(0,(int)floor(max(max(FogRange.x,0),fieldEntry-stepLength)/stepLength+offset));
    [loop]for(int i=first;i<49&&max(0,(i-offset)*stepLength)<marchEnd;++i){
        float start=max(0,(i-offset)*stepLength);
        float end=min((i+1-offset)*stepLength,surface);
        float stepSize=end-start;
        float3 p=Camera.xyz+ray*((start+end)*.5);
        float2 fieldUV=(p.xy-RegionalFogInfo.xy)*RegionalFogInfo.z+.5/64;
        float heightValid=1;


        [branch]if(FogHeightControl.w>0){
            float2 node=(floor(fieldUV*64-.5)+.5)/64;
            float4 extent=tex2Dlod(FogHeightBounds,float4(node,0,0));
            float top=extent.y+max(extent.z,max(6,max(VolumeAir.z,VolumeAir.w)));
            // Unknown metadata fails open; the margin retains float rounding at the boundary.
            heightValid=extent.w<0||(extent.w>0&&(p.z>=extent.x-.25||floorReady)&&p.z<=top+.25)?1:0;
        }
        [branch]if(heightValid>0){
        float coverage;
        float4 field=regionalFogAt(fieldUV,coverage);
        float altitude=p.z-field.x;
        float lowerFloor=1e20;
        [branch]if(floorReady&&altitude<0){
            float4 floors=tex2Dlod(DirectFogFloor,float4((floor(fieldUV*64-.5)+.5)/64,0,0));
            float2 f=frac(fieldUV*64-.5);
            lowerFloor=lerp(lerp(floors.x,floors.y,f.x),lerp(floors.z,floors.w,f.x),f.y);
        }
        bool directOnly=false;
        if(directIntegration)directOnly=altitude<0&&p.z>=lowerFloor&&lowerFloor<field.x-.25;
        float valid=altitude>=0||directOnly?coverage:0;
        if(directOnly)altitude=p.z-lowerFloor;
        // Invalid samples contribute exactly zero; retain the world-grid intervals.
        [branch]if(valid>0){
            // Field height also identifies the forest policy: 5 Duskwood,
            // 2.5 STV, 1.25 other forests, .625 sparse outdoor air.
            float profile=saturate((field.w-2.5)/2.5);
            float generalForest=1-saturate((field.w-1.25)/1.25);
            float groundHeight=lerp(field.w,6,RegionalFogInfo.w*(1-profile));
            float vertical=saturate(1-altitude/max(groundHeight,.001));
            // Coverage already rejects absent/indoor nodes and out-of-field UVs.
            // Authored regional density is world-fixed. No slanted periodic wave
            // whose apparent vertical phase scrolls when the player moves.
            float groundSigma=directOnly?0:max(field.y+field.z*RegionalFogInfo.w,0)*vertical*vertical;
            // Existing 5/2.5-unit profile tags select Duskwood/STV. This separate
            // air term reaches tree crowns while preserving
            // finite ground anchoring, indoor exclusions and exact ground policy.
            float airHeight=lerp(VolumeAir.w,VolumeAir.z,profile);
            float airVertical=saturate(1-altitude/max(airHeight,.001));
            // Generic forest air has its own smoothly time-blended host density.
            // Night air must not become thinner than day air while the moon rises.
            float airBase=lerp(VolumeAir.y,VolumeAir.x,profile);
            airBase=lerp(airBase,FogColor.w,generalForest);
            // Shared outdoor haze doubled; regional additions stay independent.
            airBase=(directOnly?.0017:WeatherInfo.w)+airBase*saturate(mad(field.w,1.6,-1)); // 0.3.198 (rain): c59.w = .0017 + extra air extinction (the literal moved into the constant: no extra slot), mirrored by sigmaAt on the CPU
            float airSigma=airBase*airVertical*airVertical;
            // Fog is thin at the viewer and thickens with distance: density ramps
            // from zero at FogRange.x to full one fade length later, so the player
            // and nearby ground stay clear while the middle distance fills in.
            float nearFade=saturate(((start+end)*.5-FogRange.x)*FogRange.y);nearFade*=nearFade*(3-2*nearFade);
            float sigma=max(groundSigma+airSigma,0)*valid*nearFade;
            if(sigma>0){
                float absorb=1-exp(-sigma*stepSize);
                // A zero source has no valid shadow pass/maps to sample. Its air
                // still contributes environment scattering and extinction.
                float heightFade=saturate(altitude*(1.0/12));heightFade*=heightFade*(3-2*heightFade);
                if(dot(sunSource,1)>0){
                    float3 projected=shadowOrigin+shadowRay*((start+end)*.5);
                    bool contained=fogDirectContained(projected);
                    float visibility=directOnly&&!contained?0:fogShadowProjected(projected);
                    [branch]if(actorIntegration&&FogActorInfo.x>0&&visibility>0)visibility=fogCombineActor(visibility,tiledActor?fogActorShadowTiled(actorOrigin+actorRay*start,actorRay*stepSize):fogActorShadow(actorOrigin+actorRay*start,actorRay*stepSize));
                    directWeight+=directTransmission*absorb*(visibility*heightFade);
                }
                directTransmission*=1-absorb;
                if(!directOnly)transmittance*=1-absorb;
            }
        }
        }
    }
    float3 directScatter=sunSource*directWeight;
    // Bound only accumulated direct scattering, preserving RGB hue and leaving
    // genuine environment fog intact at night or with the source below horizon.
    // No channel of the direct contribution can exceed this source's bound.
    float peak=max(directScatter.r,max(directScatter.g,directScatter.b));
    float cap=max(FogInfo.z,.0001);
    directScatter*=cap/(cap+peak);
    // Constant environment radiance integrates analytically to skySource*(1-T).
    // Keep it independent of direct-source gain and the direct-only soft bound.
    float3 skySource=max(AmbientLight.rgb,0)*saturate(FogColor.rgb)*(SourcePolicy.x*.35);
    return float4(skySource*(1-transmittance)+directScatter,transmittance);
}

float4 MoonFloorFog(float2 uv:TEXCOORD0):COLOR0{return worldMoonFloorFog(uv,false);}
float4 MoonFloorFogActor(float2 uv:TEXCOORD0):COLOR0{return worldMoonFloorFog(uv,true);}
float4 MoonFloorFogActorTiled(float2 uv:TEXCOORD0):COLOR0{return worldMoonFloorFog(uv,true,true,true);}
