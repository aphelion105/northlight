// Distant haze toward the WORLD horizon: in front of the scene, behind local
// scattering. Terrain weight is 0 at or nearer than the start view Z (the
// scene colour is returned bit for bit) and 1 at the game's fog end; sky/WDL
// pixels have weight 1. One optical depth for both, so a far ridge meets the
// sky without a step. Band by world elevation: camera pitch/roll never moves
// it. Full below the horizon, where only the distance weight protects ground.
// No samples, passes or raymarch steps.
float3 worldHdrDecode(float3 color){return lerp(max(color,0)/12.92,pow((max(color,0)+.055)/1.055,2.4),step(.04045,color));}
float3 worldHdrEncode(float3 color){color=max(color,0);return lerp(color*12.92,1.055*pow(color,1.0/2.4)-.055,step(.0031308,color));}
float3 horizonHaze(float3 color,float2 uv,float viewZ,bool sky,bool hdrMode,float coverageTransmission) {
    float range=sky?1:saturate((viewZ-HorizonShape.y)*HorizonShape.z);
    [branch]if(range<=0||HorizonHaze.w<=0)return color;
    float3 ray=viewPositionDistance(uv,1);
    float3 world=ray.x*InverseView[0].xyz+ray.y*InverseView[1].xyz+ray.z*InverseView[2].xyz;
    // Elevation over the camera's far edge (c64.w, 0 to -sin 3 deg): a camera high over the sea sees past the
    // mathematical horizon, and the setting sun with it. Over the horizon the band hid the sky below 0 entirely, so
    // the disc was cut on the same line at any height. The mad costs no extra slot.
    float band=exp2(-max(mad(world.z,rsqrt(dot(ray,ray)),-HorizonEdge.w),0)*HorizonShape.w);
    float amount=range*range*(3-2*range)*(1-exp2(-HorizonHaze.w*band));
    // Distant forward scattering toward a low sun: Henyey-Greenstein g=.6 in
    // azimuth, amplitude sunWeight*cos(sun elevation) from the host, capped.
    // Lift of the fog colour itself, tinted by the glow hue (c35.yzw): black night fog cannot glow.
    float lobe=length(HorizonSun.zw);
    float lift=0;
    [branch]if(lobe>0){
    float azimuth=dot(world.xy,HorizonSun.zw)*rsqrt(max(dot(world.xy,world.xy),1e-12))/max(lobe,1e-6);
    lift=min(lobe*.64*pow(1.36-1.2*azimuth,-1.5),.25);
    }
    float3 haze=(hdrMode?worldHdrDecode(HorizonHaze.rgb):HorizonHaze.rgb)*mad(lift,ShadowRange.yzw,1);
    return lerp(color,haze*coverageTransmission,amount);
}
// Coverage protects alpha overlays. Additive RGB is a separate native radiance
// contribution, not opacity: leave the remaining receiver open to lighting.
float3 effectWeight(float2 uv,float3 source,bool hdrMode,out float coverageTransmission) {
    float4 effect=tex2Dlod(EffectCoverage,float4(uv,0,0));
    if(EffectCoverageInfo.y<.5){coverageTransmission=1-saturate(effect.r);return coverageTransmission;}
    coverageTransmission=1-saturate(effect.a);
    float3 remaining=max(source-effect.rgb,0);
    if(hdrMode){source=worldHdrDecode(source);remaining=worldHdrDecode(remaining);}
    return saturate(mad(remaining-source,rcp(max(source,.000001)),1))*coverageTransmission;
}
// SDR and HDR are separate compiled variants; validate the actual slot counts.
// A single bound preserves the inferred material's channel ratios. Clipping
// channels independently makes neutral shadow/GI removal saturate pastel colors.
float3 boundedAlbedo(float3 surface,float3 lighting,float transmission) {
    float3 albedo=surface/max(lighting,.15);
    return albedo*(transmission/max(max(albedo.r,max(albedo.g,albedo.b)),max(transmission,.00001)));
}
float4 worldCompositeInputs(float2 uv,float2 pixel,bool surfaceLighting,bool hdrMode,sampler2D fogInput,out float3 effect,out float4 receiver,out float3 sourceColor,bool prelit=false,bool baseLighting=false,bool depthFromScene=true,bool noLighting=false) {
    // Fetches before any flow control use texld (1 slot; tex2Dlod costs 3-4): Scene and Depth have one level and no mip filter, so texld reads what tex2Dlod(.,0) does.
    float4 original=tex2D(Scene,uv);sourceColor=original.rgb;
    effect=0;
    float3 nativeColor=original.rgb;
    float2 centerUV=depthUV(uv);
    float d=normalizedDepthFlat(centerUV);
    float liquid=waterDistance(centerUV,d);float viewZ=liquid>0?liquid:viewDistance(d);
    // Original Terrain VS uses SIGNED view Z, including right-handed cameras.
    // Unknown fog uploads the safe identity (0,1,1,0), giving T=1 exactly.
    float legacyT=mad(LegacyFog.w,saturate(pow(max(mad(viewZ*Projection.z,LegacyFog.x,LegacyFog.y),0),LegacyFog.z))-1,1);
    receiver=float4(d,viewZ,liquid,legacyT);
    bool relight=surfaceLighting&&d<.99999&&liquid<=0;
        float3 baseline=0;
        float3 bounce=0,local=0;float shadow=0,total=0,closest=1e20;
        float2 fallbackUV=0,fogFallbackUV=0;
        float ao=0; // 0.3.174: contact AO on the lighting grid (s10)
        // Fog uses the same four half-resolution texel centres, but DIFFERENT
        // depth weights and fallback. Share only coordinates/raw depth reads;
        // retain receiverDistance (including water) for each fog sample.
        float4 fogSum=0;
        float fogTotal=0,fogClosest=1e20;
        // Bilinear tent over the four surrounding half-resolution texels with
        // depth agreement, like the fog upsample: the half-resolution lattice
        // no longer shows as blocks that crawl while the camera moves.
        float2 dimensions=ScreenSize.zw;
        float2 grid=uv*dimensions-.5,base=floor(grid),fraction=grid-base;
        float center=viewDistance(d);
        float centreLuma=dot(original.rgb,float3(.299,.587,.114)),depthBits=1.4427/max(.15,center*.01);
        [branch]if(relight||!baseLighting){
        [loop]for(int tap=0;tap<4;++tap){
            float row=floor(tap*.5);
            float2 bit=float2(tap-row*2,row);
            float2 cell=clamp(base+bit,0,dimensions-1);
            float2 q=(cell+.5)/dimensions,tapUV=halfDepthTexelUV(cell);
            // Same canonical depth expression as WorldNormals/TemporalLight; AO needs no lighting history.
            float sampleZ=depthFromScene?viewDistance(normalizedDepth(tapUV)):tex2Dlod(CurrentDistance,float4(q,0,0)).r;
            float2 blend=lerp(1-fraction,fraction,bit);
            if(relight){
            float delta=abs(sampleZ-center);
            float tapLuma=dot(tex2Dlod(Scene,float4(tapUV,0,0)).rgb,float3(.299,.587,.114));
            float guide=16*abs(centreLuma-tapLuma)/(centreLuma+tapLuma+.02),bits=delta*depthBits;
            float weight=blend.x*blend.y*exp2(-bits-guide);
            // The fallback ranks depth at a quarter: at a wall foot a top-row ground pixel is several depth tolerances from the
            // ground texels, and the full depth weight would pick a wall texel again (the light line; measured in test_colour_tent_2d).
            float score=mad(bits,.25,guide);
            float4 light=noLighting?0:tex2Dlod(LightingBuffer,float4(q,0,0));
            float3 lit=noLighting?0:tex2Dlod(BaselineLighting,float4(q,0,0)).rgb;
            float occlusion=tex2Dlod(AmbientOcclusion,float4(q,0,0)).a;
            if(prelit)light=tex2Dlod(PrelitDaylight,float4(q,0,0));
            bounce=mad(light.rgb,weight,bounce);
            [branch]if(!baseLighting&&CompositeWork.y<.5)local=mad(tex2Dlod(CurrentLocalLighting,float4(q,0,0)).rgb,weight,local);
            shadow=mad(light.a,weight,shadow);baseline=mad(lit,weight,baseline);ao=mad(occlusion,weight,ao);total+=weight;
            if(score<closest){closest=score;fallbackUV=q;}
            }
            if(!baseLighting){
            float receiverZ=sampleZ;
            if(WaterInfo.x>=.5){
                float2 water=tex2Dlod(WaterMask,float4(tapUV,0,0)).rg;
                if(water.x>ImageClip.z&&water.x<=sampleZ+max(.012,water.x*.0007)&&water.y>.003)receiverZ=water.x;
            }
            float fogDelta=abs(receiverZ-viewZ);
            float4 fogSample=float4(0,0,0,1);
            [branch]if(!baseLighting&&CompositeWork.x<.5)fogSample=tex2Dlod(fogInput,float4(q,0,0));
            float fogWeight=blend.x*blend.y*exp(-fogDelta/max(.2,viewZ*.02));
            fogSum=mad(fogSample,fogWeight,fogSum);fogTotal+=fogWeight;
            if(fogDelta<fogClosest){fogClosest=fogDelta;fogFallbackUV=q;}
            }
        }
        }
    // Retain only fallback coordinates across the loop; fetch colours only on fallback.
    float4 fogFallback=float4(0,0,0,1);
    [branch]if(!(fogTotal>.00001)&&fogClosest<1e20&&!baseLighting&&CompositeWork.x<.5)fogFallback=tex2Dlod(fogInput,float4(fogFallbackUV,0,0));
    float4 fog=baseLighting?float4(0,0,0,1):fogTotal>.00001?fogSum*(1/fogTotal):fogFallback;
    float4 fallbackLight=0;float3 fallbackBase=0;float fallbackAO=1;
    [branch]if(relight&&total<.02&&closest<1e20){
        fallbackLight=noLighting?0:tex2Dlod(LightingBuffer,float4(fallbackUV,0,0));
        if(prelit)fallbackLight=tex2Dlod(PrelitDaylight,float4(fallbackUV,0,0));
        fallbackBase=noLighting?0:tex2Dlod(BaselineLighting,float4(fallbackUV,0,0)).rgb;
        fallbackAO=tex2Dlod(AmbientOcclusion,float4(fallbackUV,0,0)).a;
    }
    // Native distance fog is already in Scene. AO belongs to its transported
    // surface term, never to atmospheric radiance in front of that surface.
    // Unknown fog is the identity. The same split also bounds relighting below.
    ao=relight?(total<.02?fallbackAO:ao/total):1;
    float3 fogPart=min((1-legacyT)*LegacyFogColor.rgb,original.rgb);
    float3 hdrSurface=0;
    if(hdrMode){
        // Lighting buffers are relative irradiance coefficients. Infer the
        // effective albedo from linear transported RGB, preserving the native
        // fog contribution instead of treating fog as a reflective material.
        float3 nativeLinear=worldHdrDecode(original.rgb);
        float3 surfaceLinear=nativeLinear;
        [branch]if(!baseLighting||CompositeWork.w<.5)
            surfaceLinear=worldHdrDecode(max(original.rgb-fogPart,0));
        hdrSurface=surfaceLinear*ao;
        float3 baseLinear=nativeLinear;
        // Match HdrDecodeWorld's raw clear-depth test. The horizon occupies
        // .94..1 and must retain native geometry even outside the world band.
        [branch]if(MaterialOcclusionInfo.z<.5||tex2Dlod(Depth,float4(uv,0,0)).r>(1.0-1.0/16777216.0))
            baseLinear=tex2Dlod(HdrLinearBase,float4(uv,0,0)).rgb;
        fogPart=(nativeLinear-surfaceLinear)*baseLinear/max(nativeLinear,.000001);
        nativeColor=baseLinear;
        original.rgb=mad(max(baseLinear-fogPart,0),ao,fogPart);
    }else{
        original.rgb=mad(original.rgb-fogPart,ao,fogPart);
        float3 bloom=tex2D(AmbientOcclusion,uv).rgb;
        original.rgb=saturate(mad(bloom,1-saturate(original.rgb),original.rgb));
    }
    float3 color=original.rgb;
    if(relight){
        // Thin receivers whose depth matches none of the four texels keep the
        // nearest-depth texel instead of losing their shadow and GI entirely.
        if(total<.02){bounce=fallbackLight.rgb;[branch]if(!baseLighting&&CompositeWork.y<.5)local=tex2Dlod(CurrentLocalLighting,float4(fallbackUV,0,0)).rgb;shadow=fallbackLight.a;baseline=fallbackBase;total=1;}
        float inverseWeight=1/total; // The fallback above sets total to one when it is below .02.
        bounce*=inverseWeight*(prelit?PrelitDaylightInfo.x:1);shadow*=inverseWeight;

        // The framebuffer already contains atmospheric fog. Relight only the
        // transported surface term: do not infer material albedo from fog RGB.
        float3 oldLight=max(baseline*inverseWeight,.15);
        float3 transported=max(original.rgb-fogPart,0);
        // A room-bank receiver has no regional directional light in its native
        // base. Including that light in the divisor suppresses lamp contrast.
        float3 ambientBase=min(oldLight,max(AmbientLight.rgb,.15));
        float3 materialLight=prelit&&PrelitDaylightInfo.z>.5?ambientBase:oldLight;
        float3 albedoT=boundedAlbedo(hdrMode?hdrSurface:transported,materialLight,legacyT);
        // Prelit RGB retains authored illumination at zero GI blend. Resident
        // probes attenuate that layer and add bounce independently of daylight.
        // As for local lamps, native transported RGB supplies a bounded material
        // estimate; this is not an extraction of the original diffuse texture.
        color=mad(albedoT,bounce,original.rgb);
        // A proven room light bank includes authored fill, not just outdoor
        // ambient. Preserve it: probe occlusion cannot identify its components.
        // Baked receivers can blend measured occlusion; current shadowed lamps
        // and incoming daylight remain independent of that ambient correction.
        float3 luma=float3(.2126,.7152,.0722);
        float ambientShare=saturate(dot(AmbientLight.rgb,luma)/dot(oldLight,luma));
        if(prelit){
            // The probe measures ambient occlusion. The native baked surface
            // also contains directional lighting; remove only its ambient share.
            color=mad(-saturate(shadow)*PrelitDaylightInfo.x*(1-PrelitDaylightInfo.y)*ambientShare,transported,color);
        }
        // Bound combined GI/shadow darkening relative to the existing surface;
        // this preserves black and does not introduce an absolute exposure floor.
        // Prelit daylight and bounce are additive and its ambient subtraction
        // is already bounded above. Only signed outdoor correction needs this.
        if(!prelit){
            // GI can replace the entire ambient share. The native directional
            // share keeps its own removal limit, including when GI is enabled.
            float limit=lerp(directRemovalLimit(),shadowRemovalLimit(),ambientShare);
            color=max(color,mad(-limit,transported,original.rgb));
        }
        // Bound shadow/GI removal before adding current local radiance, so a
        // darkening floor cannot swallow a weaker overlapping lamp channel.
        // Compress the final, shadowed lamp sum once in SDR, preserving hue.
        // HDR radiance remains available to the tone mapper and bloom.
        if(!baseLighting){
        float3 localAdd=max(albedoT*(local*inverseWeight),0);
        // A dark native surface is not a cap on incoming lamp radiance. HDR
        // retains it for the tone mapper; SDR uses its remaining display room.
        float3 localRoom=max(1-color,0);
        float3 localRatio=localAdd/max(localRoom,.00001);
        float localScale=1/(1+max(localRatio.r,max(localRatio.g,localRatio.b)));
        color+=hdrMode?localAdd:min(localAdd*localScale,localRoom);
        }
        // NightBrightness: gives back a share of the art layer's darker night ambient (x .82 x cool tint) as light: albedoT times
        // share x ambient x (1/factor-1), the per-frame product computed on the CPU (night_floor.h nightAmbient). Added like
        // light, so shadows, GI and lamps keep their contrast. 0 = off (bit-identical).
        if(!prelit)color=mad(albedoT,GridOrigin.rgb,color);
        if(PassInfo.z==1)color=shadow.xxx;
        if(PassInfo.z==2)color=max(AmbientLight.rgb+bounce,0);
    }
    // The captured scene already contains legacy distance fog. Local integrated
    // radiance lies IN FRONT of that background, including sky/fogged terrain.
    // Endpoint legacyT must not extinguish scattering produced near the camera.
    // Retain fogPart only for safe surface relighting above; never de-fog/divide.
    // Horizon haze extinguishes the far scene (including the depth-occluded
    // sun/moon disc) BEFORE the local scattering is added in front of it.
    float coverageTransmission=1;float3 receiverTransmission=1;
    [branch]if(min(EffectCoverageInfo.x,1-PassInfo.z)>.5)receiverTransmission=effectWeight(uv,tex2D(Scene,uv).rgb,hdrMode,coverageTransmission);
    effect=1-receiverTransmission;
    color*=receiverTransmission;
    if(PassInfo.z<.5){
        float3 unfogged=color;
        // Rain streaks were drawn into the scene before the composite: on mask pixels go back toward the unfogged pixel so they are not hazed.
        float rain=0;
        [branch]if(!baseLighting||CompositeWork.z<.5)rain=tex2Dlod(RainMask,float4(uv,0,0)).r;
        float3 hazed=horizonHaze(color,centerUV,viewZ,d>=.99999&&liquid<=0,hdrMode,coverageTransmission);
        float3 fogged=mad(hazed,fog.a,fog.rgb*coverageTransmission);
        // Forever's decoded colours are radiance in both output modes. SDR
        // must composite in linear space too, then encode for its UNORM target.
        if(!hdrMode){[branch]if(MaterialOcclusionInfo.w>.5)
            fogged=worldHdrEncode(mad(worldHdrDecode(hazed),fog.a,fog.rgb*coverageTransmission));}
        color=lerp(fogged,unfogged,rain);
    }
    if(PassInfo.z==3)color=fog.rgb;
    // Native emission is preserved separately; atmospheric in-scattering
    // depends only on alpha coverage, never on the emitted RGB fraction.
    color=mad(nativeColor,effect,color);
    // Dither before the 8-bit target rounds: haze and night air vary only with view elevation and would band into
    // horizontal lines. VPOS is the integer pixel; +.5 texel hits the POINT texel centre.
    if(hdrMode)return float4(max(color,0),original.a);
    return float4(max(color+tex2D(DitherNoise,mad(pixel,1.0/64,.5/64)).rgb,0),original.a);
}

float4 worldComposite(float2 uv,float2 pixel,bool surfaceLighting,bool hdrMode,sampler2D fogInput,out float3 effect,bool prelit=false,bool baseLighting=false,bool depthFromScene=true,bool noLighting=false) {float4 receiver;float3 source;return worldCompositeInputs(uv,pixel,surfaceLighting,hdrMode,fogInput,effect,receiver,source,prelit,baseLighting,depthFromScene,noLighting);}
float4 WorldComposite(float2 uv:TEXCOORD0,float2 pixel:VPOS):COLOR0 {float3 effect;return worldComposite(uv,pixel,true,false,FogBuffer,effect);}
// Self-lit M2 receivers keep their native radiance under the same atmosphere.
// Selected with the existing depth-stencil; no additional image or geometry replay.
float4 WorldUnlitComposite(float2 uv:TEXCOORD0,float2 pixel:VPOS):COLOR0 {float3 effect;return worldComposite(uv,pixel,false,false,FogBuffer,effect);}
float4 WorldCompositeHdr(float2 uv:TEXCOORD0,float2 pixel:VPOS):COLOR0 {float3 effect;return worldComposite(uv,pixel,true,true,FogBuffer,effect);}
// With no volume/local buffers, retain AO and the same colour-guided light
// upsample. Native fog, weather haze and coverage still use their normal path.
float4 WorldBaseCompositeHdr(float2 uv:TEXCOORD0,float2 pixel:VPOS):COLOR0 {float3 effect;return worldComposite(uv,pixel,true,true,FogBuffer,effect,false,true);}
// No lighting producer: its two inputs are proven zero, while AO and night lift remain unchanged.
float4 WorldAOCompositeHdr(float2 uv:TEXCOORD0,float2 pixel:VPOS):COLOR0 {float3 effect;return worldComposite(uv,pixel,true,true,FogBuffer,effect,false,true,true,true);}
float4 WorldUnlitCompositeHdr(float2 uv:TEXCOORD0,float2 pixel:VPOS):COLOR0 {float3 effect;return worldComposite(uv,pixel,false,true,FogBuffer,effect);}
float4 WorldPrelitComposite(float2 uv:TEXCOORD0,float2 pixel:VPOS):COLOR0 {float3 effect;return worldComposite(uv,pixel,true,false,FogBuffer,effect,true);}
float4 WorldPrelitCompositeHdr(float2 uv:TEXCOORD0,float2 pixel:VPOS):COLOR0 {float3 effect;return worldComposite(uv,pixel,true,true,FogBuffer,effect,true);}

float moonTerrainSpecular(float2 uv,float distance,float3 position,float3 normal,float3 detail){
    float4 capture=tex2Dlod(BumpMaterial,float4(uv,0,0));
    if(capture.r>=0||capture.a<=.02||abs(abs(capture.b)/max(capture.a,.02)-distance)>=max(.025,distance*.0015))return 0;
    float3 eye=-normalize(position);
    float3 light=MaterialDirection[1].xyz;
    float2 across=float2(-light.y,light.x);
    float side=dot(eye.xy,across);
    float height=eye.z-light.z-dot(normal.xy,light.xy);
    float lobe=exp2(-180*side*side-5*height*height)*saturate(-4*dot(eye.xy,light.xy));
    // Static wind ripples in world space. Filter them into a weak sheen at
    // grazing distances where successive crests cover only a few pixels.
    float2 ground=Camera.xy+position.xy;
    float warp=frac(dot(ground,float2(-.068,.15)))-.5;
    float phase=dot(ground,float2(.79,.40))+1.1*warp*(.5-abs(warp));
    float crest=saturate(1-3.6*abs(frac(phase)-.5));
    float surfaceAngle=saturate(dot(normal,eye));
    float resolved=saturate(surfaceAngle*ScreenSize.y/(22*distance));
    float crestWeight=resolved*resolved;
    // Lift partially resolved crests while keeping the grazing fade and near peaks.
    crestWeight*=1+.8*(1-crestWeight);
    float ridge=lerp(.26,.17+1.10*crest*crest,crestWeight);
    float grain=saturate(.88+2*dot(detail-normal,light));
    float grazing=1-.8*surfaceAngle;grazing*=grazing;
    float fade=saturate((MaterialColor[1].w-distance)*.0125);
    return lobe*grazing*ridge*grain*saturate(4*(normal.z-.65))*fade*MaterialDirection[1].w;
}
float sunSurfaceSpecular(float2 uv,float distance,float3 position,float3 normal,float3 detail){
    float4 capture=tex2Dlod(BumpMaterial,float4(uv,0,0));
    bool terrain=capture.r<0;
    if((!terrain&&capture.g>=0)||capture.a<=.02||
       abs(abs(capture.b)/max(capture.a,.02)-distance)>=max(.025,distance*.0015))return 0;
    float3 light=MaterialDirection[0].xyz;
    float3 eye=-normalize(position);
    float3 halfVector=normalize(light+eye);
    float lobe=pow(max(saturate(dot(normalize(detail),halfVector)),.00001),terrain?24:40);
    float front=saturate(4*min(dot(normal,light),dot(normal,eye)));
    float fade=saturate((BumpControl.y-distance)/max(.25*BumpControl.y,1));
    return lobe*front*fade*(terrain?.75:.48)*MaterialDirection[0].w;
}
// Full-resolution material relighting. The base composition has already handled
// shadows, GI, AO and fog; only the normal-dependent directional difference is
// added here. Both the inferred albedo and atmospheric transmission are bounded.
float4 materialComposite(float2 uv,bool hdrMode,float4 color,float3 effect,bool reuseInputs=false,float4 receiver=0,float3 sharedSource=0,bool solar=false) {
    float2 q=depthUV(uv);float d=reuseInputs?receiver.x:normalizedDepth(q);float z=reuseInputs?receiver.y:viewDistance(d);
    if(d>=.99999||(reuseInputs?receiver.z:waterDistance(q,d))>0||(BumpControl.x<=0&&MaterialOcclusionInfo.x<=0))return color;
    float occlusionRange=MaterialOcclusionInfo.y>0?MaterialOcclusionInfo.y:BumpControl.y;
    const float specularRange=reuseInputs?0:solar?BumpControl.y:MaterialColor[1].w;
    const bool terrainGlint=!reuseInputs&&z<specularRange;
    [branch]if((BumpControl.x<=0||z>=BumpControl.y)&&(MaterialOcclusionInfo.x<=0||z>=occlusionRange)&&!terrainGlint)return float4(max(color.rgb,0),color.a);
    float3 v=viewPositionDistance(q,z);
    // Camera-relative derivatives avoid losing sub-centimetre detail at large map coordinates.
    float3 relative=v.x*InverseView[0].xyz+v.y*InverseView[1].xyz+v.z*InverseView[2].xyz;
    // A half-resolution normal may belong to the facade behind this pixel.
    // Keep normal, baseline and visibility on a depth-compatible receiver.
    float2 materialUV=uv;
    float2 normalUV=uv,base=floor(uv*ScreenSize.zw-.5);float normalScore=1e20;
    [loop]for(int tap=0;tap<4;++tap){
        float row=floor(tap*.5);
        float2 cell=clamp(base+float2(tap-row*2,row),0,ScreenSize.zw-1);
        float2 candidate=(cell+.5)/ScreenSize.zw;
        // The combined pass already binds this half-grid's canonical distance.
        float candidateZ=reuseInputs?tex2Dlod(CurrentDistance,float4(candidate,0,0)).r:viewDistance(normalizedDepth(halfDepthTexelUV(cell)));
        float gap=abs(candidateZ-z);
        // Among compatible receivers retain the nearest pixel, not a depth
        // minimum that would pull a grazing plane toward the camera.
        float2 offset=(candidate-uv)*ScreenSize.zw;
        float score=dot(offset,offset)+(gap>max(.025,z*.0015)?100:0);
        if(score<normalScore){normalScore=score;normalUV=candidate;}
    }
    // A grazing plane can cross the half-resolution depth tolerance every
    // other row. The glint has its own full-resolution terrain/depth proof.
    if(normalScore>=100&&!terrainGlint)return color;
    if(any(floor(normalUV*ScreenSize.zw)!=floor(uv*ScreenSize.zw)))materialUV=normalUV;
    float3 normal=tex2Dlod(NormalBuffer,float4(normalUV,0,0)).xyz;
    float3 detail=autoBumpNormal(q,relative,normal,z,InverseView[0].xyz,InverseView[1].xyz,InverseView[2].xyz);
    float3 occlusion=1;
    [branch]if(MaterialOcclusionInfo.x>0&&z<occlusionRange){
        float4 material=tex2Dlod(BumpMaterial,float4(q,0,0)),occ=tex2Dlod(MaterialOcclusion,float4(q,0,0));
        bool packed=material.a>1;
        float cover=saturate(material.a);
        float capturedZ=abs(material.b)/max(cover,.001);
        float valid=min(cover,packed?1:occ.a)>.02&&abs(capturedZ-z)<max(.025,z*.0015);
        float fade=saturate((occlusionRange-z)/max(occlusionRange*.25,1));
        float3 value=packed?float3(1,1,mad(material.a,256.0/255,-257.0/255)):saturate(occ.rgb/max(occ.a,.001));
        occlusion=lerp(1,value,valid*fade);
    }
    float3 delta=0;
    [unroll]for(int i=0;i<2;++i)
        delta+=MaterialColor[i].rgb*(saturate(dot(detail,MaterialDirection[i].xyz))*occlusion[i]-saturate(dot(normal,MaterialDirection[i].xyz)));
    float visibility=saturate((tex2Dlod(LightingBuffer,float4(materialUV,0,0)).a*WaterInfo.z-(1-GridInfo.z))/max(GridInfo.z,.001));
    float legacyT=reuseInputs?receiver.w:mad(LegacyFog.w,saturate(pow(max(mad(z*Projection.z,LegacyFog.x,LegacyFog.y),0),LegacyFog.z))-1,1);
    float3 source=reuseInputs?sharedSource:tex2D(MaterialSource,uv).rgb;
    float3 surface=max(source-min((1-legacyT)*LegacyFogColor.rgb,source),0);
    if(hdrMode)surface=worldHdrDecode(surface);
    surface*=tex2D(AmbientOcclusion,uv).a;
    float3 albedoT=boundedAlbedo(surface,tex2D(BaselineLighting,materialUV).rgb,legacyT);
    float3 cavity=max(AmbientLight.rgb*AmbientLight.w+GridOrigin.rgb,0)*(occlusion.b-1);
    float3 transmission=1-effect;
    float3 materialLight=normalScore<100?albedoT*(delta*visibility+cavity):0;
    [branch]if(terrainGlint){
        if(solar)materialLight+=MaterialColor[0].rgb*(sunSurfaceSpecular(q,z,relative,normal,detail)*visibility*legacyT*occlusion.r);
        else materialLight+=MaterialColor[1].rgb*(moonTerrainSpecular(q,z,relative,normal,detail)*visibility*legacyT*occlusion.g);
    }
    color.rgb=max(color.rgb+materialLight*tex2D(MaterialFog,uv).a*transmission,0);
    return color;
}
float3 materialEffectWeight(float2 uv,bool hdrMode){
    float3 transmission=1;float coverageTransmission;[branch]if(EffectCoverageInfo.x>.5)transmission=effectWeight(uv,tex2D(MaterialSource,uv).rgb,hdrMode,coverageTransmission);return 1-transmission;
}
float4 MaterialComposite(float2 uv:TEXCOORD0):COLOR0 {return materialComposite(uv,false,tex2D(Scene,uv),materialEffectWeight(uv,false));}
float4 MaterialCompositeHdr(float2 uv:TEXCOORD0):COLOR0 {return materialComposite(uv,true,tex2D(Scene,uv),materialEffectWeight(uv,true));}
float4 MaterialSolarComposite(float2 uv:TEXCOORD0):COLOR0 {
    return materialComposite(uv,false,tex2D(Scene,uv),materialEffectWeight(uv,false),false,0,0,true);
}
float4 MaterialSolarCompositeHdr(float2 uv:TEXCOORD0):COLOR0 {
    return materialComposite(uv,true,tex2D(Scene,uv),materialEffectWeight(uv,true),false,0,0,true);
}
// s4 carries fog while s9 carries material height. Keep the intermediate color
// in registers instead of a full-resolution FP16 store/read between passes.
float4 WorldMaterialCompositeHdr(float2 uv:TEXCOORD0,float2 pixel:VPOS):COLOR0 {
    // Material detail already requires the lighting distance target. Retain its
    // read here to keep this combined SM3 entry below its device slot limit.
    float3 effect,source;float4 receiver;float4 color=worldCompositeInputs(uv,pixel,true,true,MaterialFog,effect,receiver,source,false,false,false);
    return materialComposite(uv,true,color,effect,true,receiver,source);
}

// Sampled diagnostic only: nine small occlusion queries count a regular grid,
// using the same depth validity and near cutoff as MaterialComposite.
// BumpControl.w selects capture/effect counts (0..5) or scene range coverage (6..8).
float4 MaterialAudit(float2 uv:TEXCOORD0):COLOR0 {
    float2 q=depthUV(uv);float d=normalizedDepth(q);float z=viewDistance(d);
    if(BumpControl.w>5.5){
        clip((d<.99999&&waterDistance(q,d)<=0)?1:-1);
        float limit=BumpControl.w>7.5?MaterialOcclusionInfo.y:BumpControl.w>6.5?BumpControl.y-max(BumpControl.y*.25,1):BumpControl.y;
        clip(z<limit?1:-1);return 0;
    }
    float4 material=tex2Dlod(BumpMaterial,float4(q,0,0));
    bool packed=material.a>1;
    float cover=saturate(material.a);
    float valid=cover>.02&&abs(abs(material.b)/max(cover,.001)-z)<max(.025,z*.0015);
    if(BumpControl.w>3.5){
        clip(cover>.02?1:-1);
        if(BumpControl.w>4.5)clip(valid?-1:1);
        return 0;
    }
    clip((valid&&d<.99999&&waterDistance(q,d)<=0)?1:-1);
    if(BumpControl.w>.5&&BumpControl.w<1.5){
        float3 v=viewPositionDistance(q,z);
        float3 relative=v.x*InverseView[0].xyz+v.y*InverseView[1].xyz+v.z*InverseView[2].xyz;
        float3 normal=tex2Dlod(NormalBuffer,float4(uv,0,0)).xyz;
        float3 detail=autoBumpNormal(q,relative,normal,z,InverseView[0].xyz,InverseView[1].xyz,InverseView[2].xyz);
        clip(length(detail-normal)>.001?1:-1);
    }
    if(BumpControl.w>1.5){
        float occlusionRange=MaterialOcclusionInfo.y>0?MaterialOcclusionInfo.y:BumpControl.y;
        float4 occ=tex2Dlod(MaterialOcclusion,float4(q,0,0));
        float3 value=packed?float3(1,1,mad(material.a,256.0/255,-257.0/255)):saturate(occ.rgb/max(occ.a,.001));
        clip((MaterialOcclusionInfo.x>0&&z<occlusionRange&&(packed||occ.a>.02))?1:-1);
        clip((BumpControl.w<2.5?value.b:min(value.r,value.g))<.999?1:-1);
    }
    return 0;
}

