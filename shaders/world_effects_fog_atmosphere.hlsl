sampler2D FogHeightBounds : register(s12); // footprint min/max ground, max layer height, validity
float4 FogHeightControl : register(c65); // xy: global vertical support, z: global validity, w: matching per-cell bounds
float4 worldFog(float2 uv,bool actorIntegration,bool directIntegration=true,bool tiledActor=false) {
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
    float transmittance=1,directWeight=0;
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
            heightValid=extent.w<0||(extent.w>0&&p.z>=extent.x-.25&&p.z<=top+.25)?1:0;
        }
        [branch]if(heightValid>0){
        float coverage;
        float4 field=regionalFogAt(fieldUV,coverage);
        float altitude=p.z-field.x;
        float valid=altitude>=0?coverage:0;
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
            float groundSigma=max(field.y+field.z*RegionalFogInfo.w,0)*vertical*vertical;
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
            airBase=WeatherInfo.w+airBase*saturate(mad(field.w,1.6,-1)); // 0.3.198 (rain): c59.w = .0017 + extra air extinction (the literal moved into the constant: no extra slot), mirrored by sigmaAt on the CPU
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
                    float visibility=fogShadowProjected(shadowOrigin+shadowRay*((start+end)*.5));
                    [branch]if(actorIntegration&&FogActorInfo.x>0&&visibility>0)visibility=fogCombineActor(visibility,tiledActor?fogActorShadowTiled(actorOrigin+actorRay*start,actorRay*stepSize):fogActorShadow(actorOrigin+actorRay*start,actorRay*stepSize));
                    // Sun shafts reach the ground; the medium and shadow map already bound them.
                    directWeight+=transmittance*absorb*(visibility*lerp(1,heightFade,SunDirection.w));
                }
                transmittance*=1-absorb;
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

// Local Forever DB2 import. Its own medium replaces regional artistic fog.
// c116..153 avoid the static-shadow transition bank c80..100 and c115.
float4 Forever[38] : register(c116);
float3 foreverNoise(float3 p) {
    float3 noise=1;
    // A noisy layer selects this shader for the whole volume. Other layers
    // still have exact unit noise; do not fetch their two unused octaves.
    [unroll]for(int i=0;i<3;++i){
        [branch]if(Forever[34][i]!=0){
            float a=tex3Dlod(CloudNoise,float4((p-PassInfo.w*Forever[25+i].xyz)*Forever[24][i],0)).r;
            float b=tex3Dlod(CloudNoise,float4((p-PassInfo.w*Forever[28+i].xyz)*Forever[31][i],0)).r;
            noise[i]=lerp(1,saturate(2*(a*Forever[32][i]+b*Forever[33][i])),Forever[34][i]);
        }
    }
    return noise;
}
float foreverVisibility(float3 ray,float a,float b,float t,bool actorIntegration,bool cullUnneeded,bool skipEmpty=false){
    float visible=1;
    [branch]if(!cullUnneeded||any(Forever[9].xyz!=0)){
        visible=fogShadowProjected(affinePoint(Camera.xyz+ray*t,FarMatrix));
        if(actorIntegration&&FogActorInfo.x>0&&visible>0)visible=fogCombineActor(visible,(skipEmpty?fogActorShadowTiled(affinePoint(Camera.xyz+ray*a,FogActorMatrix),(ray.x*FogActorMatrix[0].xyz+ray.y*FogActorMatrix[1].xyz+ray.z*FogActorMatrix[2].xyz)*(b-a),true):fogActorShadow(affinePoint(Camera.xyz+ray*a,FogActorMatrix),(ray.x*FogActorMatrix[0].xyz+ray.y*FogActorMatrix[1].xyz+ray.z*FogActorMatrix[2].xyz)*(b-a))));
    }
    return visible;
}
float4 foreverFog(float2 uv,bool actorIntegration,bool samplesNoise=true,bool skipEmpty=false) {
    // The noisy actor and transition variants exhaust SM3 temporaries with early culling.
#if NORTHLIGHT_SHADOW_TRANSITION
    const bool earlyCull=!samplesNoise;
#else
    const bool earlyCull=!samplesNoise||!actorIntegration;
#endif
    if(FogInfo.x<.5)return float4(0,0,0,1);
    uv=depthUV(uv);float depth=normalizedDepth(uv);
    float3 view=viewPositionDistance(uv,fogReceiverDistance(uv,depth));
    float3 ray=view.x*InverseView[0].xyz+view.y*InverseView[1].xyz+view.z*InverseView[2].xyz;
    float distance=min(length(ray),Forever[1].w);ray*=rsqrt(max(dot(ray,ray),1e-12));
    float3 g=Forever[6].xyz;
    // HG integrates to one over solid angle; imported intensity is not a
    // sphere-average radiance. Omitting 1/(4*pi) amplifies it by 12.57x.
    float3 phase=(1.0/(4*3.14159265))*(1-g*g)*pow(rsqrt(max(1+g*g-2*g*dot(ray,SunDirection.xyz),1e-6)),3);
    float3 direct=phase*Forever[11].xyz*Forever[11].w;
    float T=1;float3 scatter=0;
    // Quadratic spacing resolves near shafts while retaining the distant haze.
    [loop]for(int j=0;j<48;++j){
        float a=distance*(j*j)*(1.0/2304),b=distance*((j+1)*(j+1))*(1.0/2304),t=(a+b)*.5;
        // Zero-density prefix; preserve the original grid and boundary samples.
        if(samplesNoise&&t<Forever[34].w)continue;
        [branch]if(samplesNoise||t>=Forever[34].w){
        float visible=1;
        if(!earlyCull)visible=foreverVisibility(ray,a,b,t,actorIntegration,false,skipEmpty);
#if NORTHLIGHT_SHADOW_TRANSITION
        float3 p=Camera.xyz+ray*t;float coverage;
        float4 field=regionalFogAt((p.xy-RegionalFogInfo.xy)*RegionalFogInfo.z+.5/64,coverage);
        float fieldGround=field.x;
        float inside=all(p.xy>=RegionalFogInfo.xy)&&all(p.xy<=RegionalFogInfo.xy+63.0/(64*max(RegionalFogInfo.z,1e-8)));
#else
        // This ordering keeps the sparse actor variant within 32 SM3 temporaries.
        float3 p=Camera.xyz+ray*t;
        float inside=all(p.xy>=RegionalFogInfo.xy)&&all(p.xy<=RegionalFogInfo.xy+63.0/(64*max(RegionalFogInfo.z,1e-8)));
        float coverage=0,fieldGround=Forever[0].w;
        [branch]if(inside){float4 field=regionalFogAt((p.xy-RegionalFogInfo.xy)*RegionalFogInfo.z+.5/64,coverage);fieldGround=field.x;}
#endif
        // The regional height is the top WMO/terrain envelope, not a solid
        // column: a roof must not erase global fog in the air below it.
        // Coverage still excludes masked regions; authored profiles own Z.
        float valid=inside?coverage:1;
        [branch]if(!earlyCull||valid>0){
        float ground=coverage>0?fieldGround:Forever[0].w;
        if(earlyCull)visible=foreverVisibility(ray,a,b,t,actorIntegration,true,skipEmpty);
        float3 height=p.z-ground*Forever[10].xyz;
        float3 shape=exp(min(0,(Forever[2].xyz-height)*Forever[3].xyz)+min(0,(height-Forever[4].xyz)*Forever[5].xyz));
        float3 curve=1+Forever[7].xyz*pow(max(saturate((t-Forever[1].xyz)*.001),1e-8),Forever[8].xyz);
        float3 density=Forever[0].xyz*shape*curve*step(Forever[1].xyz,t)*valid;
        float3 noise=1;
        // No layer can scatter when its pre-noise density is exactly zero.
        // Keep the actor path within SM3's register limit by gating only noise.
        [branch]if(samplesNoise&&dot(density,1)>0)noise=foreverNoise(p);
        density*=noise;
        float sigma=dot(density,1),absorb=1-exp(-sigma*(b-a));
        float3 lit=lerp(1,visible,Forever[9].xyz)*direct;
        float3 ambient=SourcePolicy.x*lerp(1,visible,Forever[9].xyz);
        float3 shade=SourcePolicy.x*(1-lerp(1,visible,Forever[9].xyz));
        float3 source0=Forever[12].rgb*lit.x+lerp(Forever[35].rgb,Forever[15].rgb,noise.x)*ambient.x+Forever[18].rgb*shade.x;
        float3 source1=Forever[13].rgb*lit.y+lerp(Forever[36].rgb,Forever[16].rgb,noise.y)*ambient.y+Forever[19].rgb*shade.y;
        float3 source2=Forever[14].rgb*lit.z+lerp(Forever[37].rgb,Forever[17].rgb,noise.z)*ambient.z+Forever[20].rgb*shade.z;
        scatter+=T*absorb*(source0*density.x+source1*density.y+source2*density.z)/max(sigma,1e-8);
        T*=1-absorb;
        }
        }
    }
    return float4(scatter,T);
}
float4 ForeverFog(float2 uv:TEXCOORD0):COLOR0{return foreverFog(uv,false);}
float4 ForeverFogActor(float2 uv:TEXCOORD0):COLOR0{return foreverFog(uv,true);}
float4 ForeverFogNoNoise(float2 uv:TEXCOORD0):COLOR0{return foreverFog(uv,false,false);}
float4 ForeverFogActorNoNoise(float2 uv:TEXCOORD0):COLOR0{return foreverFog(uv,true,false);}
float4 ForeverFogActorTiled(float2 uv:TEXCOORD0):COLOR0{return foreverFog(uv,true,true,true);}
float4 ForeverFogActorTiledNoNoise(float2 uv:TEXCOORD0):COLOR0{return foreverFog(uv,true,false,true);}

