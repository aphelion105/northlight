// Static probe-to-receiver visibility. s13 is idle during the GI pass.
sampler2D LocalVisibility : register(s13);
float4 GICascadeInfo : register(c83); // w: second layer present; zero preserves single-grid captures
float4 LocalVisibilityInfo : register(c82); // columns, reciprocal width/height, reciprocal columns
float localPlaneVisibility(float3 plane,float3 delta){
    return saturate((.04-(dot(plane,delta)-1)*rsqrt(max(dot(plane,plane),1e-16)))*25);
}
float localProbeVisibility(float index,float3 delta){
    float2 tile=float2(index-floor(index*LocalVisibilityInfo.w)*LocalVisibilityInfo.x,floor(index*LocalVisibilityInfo.w))*9;
    float3 d=delta/max(dot(abs(delta),1),1e-6);
    if(d.z<0)d.xy=(1-abs(d.yx))*float2(d.x>=0?1:-1,d.y>=0?1:-1);
    float2 q=saturate(d.xy*.5+.5)*8,base=floor(q),f=q-base;
    float2 uv=(tile+base+.5)*LocalVisibilityInfo.yz;
    float4 a=tex2Dlod(LocalVisibility,float4(uv,0,0));
    [branch]if(a.w<-.5)return 0;
    float v=localPlaneVisibility(a.xyz,delta);
#ifndef LOCAL_VISIBILITY_FOUR_READS
    [branch]if(a.w>.5){v*=v;return v*v;}
#endif
    // Clamp within this probe's tile at the four octahedral vertices.
    float2 step=min(1,8-base)*LocalVisibilityInfo.yz;
    float b=localPlaneVisibility(tex2Dlod(LocalVisibility,float4(uv+float2(step.x,0),0,0)).xyz,delta);
    float c=localPlaneVisibility(tex2Dlod(LocalVisibility,float4(uv+float2(0,step.y),0,0)).xyz,delta);
    float e=localPlaneVisibility(tex2Dlod(LocalVisibility,float4(uv+step,0,0)).xyz,delta);
    v=lerp(lerp(v,b,f.x),lerp(c,e,f.x),f.y);v*=v;return v*v;
}
float localProbeFacing(float3 normal,float3 delta){
    return max(0,(dot(normal,-delta)*rsqrt(max(dot(delta,delta),1e-10))+.15)*(1/1.15));
}

float2 probeUV(float3 cell) {
    return (float2(cell.x+cell.z*GridInfo.x,cell.y)+.5)/float2(GridInfo.x*GridInfo.x,GridInfo.x);
}
float4 probeCascadeIrradiance(float3 p,float3 n,uniform bool localOnly,float level,float layers) {
    if(GridInfo.w<.5)return 0;
    // World lattice is independent of the camera/request grid. Wrapped atlas
    // slots are usable only when metadata names this exact world probe.
    float spacing=localOnly?GridOrigin.w:GridOrigin.w*(level==0?1:4);
    float3 grid=p/spacing;
    float3 base=floor(grid),fraction=grid-base;
    float4 basis=float4(.28209479,.48860251*n.x,.48860251*n.y,.48860251*n.z);
    // SH radiance -> cosine-convolved diffuse irradiance.
    basis*=float4(3.14159265,2.0943951,2.0943951,2.0943951);
    float3 result=0;float total=0,coverage=0,validSupport=0;
    [loop]for(int i=0;i<8;++i){
        float3 bit=float3(i-floor(i/2.0)*2,floor(i/2.0)-floor(i/4.0)*2,floor(i/4.0));
        float3 cell=base+bit;
        float3 wrapped=cell-GridInfo.x*floor(cell/GridInfo.x);
        float2 uv=probeUV(wrapped);if(!localOnly)uv.y=(wrapped.y+level*GridInfo.x+.5)/(GridInfo.x*layers);
        float4 metadata=tex2Dlod(ProbeMetadata,float4(uv,0,0));
        if(all(metadata.xyz==cell)&&metadata.w!=-1){
        float3 t=lerp(1-fraction,fraction,bit);float weight=t.x*t.y*t.z;
        // First residency fades in at this world point; camera movement and
        // same-key atlas publication cannot reset its activation time.
        weight*=saturate((PassInfo.w-metadata.w)*(1/.45));
        // Interior probes establish residency too. moment.z excludes their
        // radiance below; treating them as missing paints the 8-unit lattice
        // onto terrain as the interpolation weights change across a surface.
        coverage+=weight;
        float3 pp=cell*spacing;
        float3 delta=p-pp;float distance=length(delta);
        float4 moment;float visibility;
        [branch]if(localOnly||LocalVisibilityInfo.x>0){
            float momentY=localOnly?(wrapped.y+.5)/(GridInfo.x*6):(wrapped.y+level*GridInfo.x*6+.5)/(GridInfo.x*6*layers);
            moment=tex2Dlod(ProbeVisibility,float4(uv.x,momentY,0,0));
            float index=wrapped.x+wrapped.y*GridInfo.x+wrapped.z*GridInfo.x*GridInfo.x;
            if(!localOnly)index+=level*GridInfo.x*GridInfo.x*GridInfo.x;
            // Zero support cannot contribute, even when the cell owns residency.
#ifndef GI_FORCE_VISIBILITY_READS
            visibility=0;
            [branch]if(localOnly||weight*moment.z>0)
#endif
                visibility=localProbeVisibility(index,delta)*localProbeFacing(n,delta);
        }else{
        float3 absolute=abs(delta);float axis;
        if(absolute.x>=absolute.y&&absolute.x>=absolute.z)axis=delta.x>=0?0:1;
        else if(absolute.y>=absolute.z)axis=delta.y>=0?2:3;
        else axis=delta.z>=0?4:5;
        float2 visibilityUV=float2(uv.x,(wrapped.y+(axis+level*6)*GridInfo.x+.5)/(GridInfo.x*6*layers));
        moment=tex2Dlod(ProbeVisibility,float4(visibilityUV,0,0));
        float variance=max(moment.y-moment.x*moment.x,.02);
        float excess=max(distance-moment.x,0);
        visibility=variance/(variance+excess*excess);visibility*=visibility;
        }
        weight*=moment.z;
        validSupport+=weight;
        weight*=visibility;
        // Coverage above describes residency even for occluded probes. Only
        // their zero-weight radiance/history reads can be omitted. The fused
        // localOnly entry stays unchanged to fit its 510-slot SM3 budget.
        [branch]if(localOnly||weight>0){
        float3 irradiance=float3(dot(tex2Dlod(ProbeR,float4(uv,0,0)),basis),dot(tex2Dlod(ProbeG,float4(uv,0,0)),basis),dot(tex2Dlod(ProbeB,float4(uv,0,0)),basis));
        // 0.3.197: a same-key re-publication blends from what was on screen over .3 s (moment.w = start, -1000 = none); first residency keeps born's fade.
        float blend=saturate((PassInfo.w-moment.w)*(1/.3));
        [branch]if(blend<1){float2 third=float2(uv.x*(1/3.),uv.y);
            float3 previous=float3(dot(tex2Dlod(ProbePrevious,float4(third,0,0)),basis),dot(tex2Dlod(ProbePrevious,float4(third+float2(1/3.,0),0,0)),basis),dot(tex2Dlod(ProbePrevious,float4(third+float2(2/3.,0),0,0)),basis));
            irradiance=lerp(previous,irradiance,blend);}
        result+=max(irradiance,0)*weight;total+=weight;
        }
        }
    }
    // Occluded valid probes still own darkness. When every neighbour is inside
    // solid geometry, give the coarser cascade or sky estimate the receiver.
    // Reach ordinary residency with 1/32 valid support to avoid grid seams.
    return float4(result/max(total,.00001),saturate(min(coverage,32*validSupport)));
}
float4 probeIrradiance(float3 p,float3 n,uniform bool localOnly){
    if(localOnly)return probeCascadeIrradiance(p,n,true,0,1);
    float4 accumulated=0;float layers=1+GICascadeInfo.w;
    [loop]for(int level=0;level<2&&level<layers&&accumulated.a<=.9999;++level){
        float4 sample=probeCascadeIrradiance(p,n,false,level,layers);
        if(level==0)accumulated=sample;
        else {
            float contribution=sample.a*(1-accumulated.a);
            float coverage=accumulated.a+contribution;
            accumulated=float4((accumulated.rgb*accumulated.a+sample.rgb*contribution)/max(coverage,.00001),coverage);
        }
    }
    return accumulated;
}

struct LightingOutput {float4 correction:COLOR0;float4 baseline:COLOR1;float4 daylight:COLOR2;};
float3 worldGIValue(float3 p,float4 smoothNormal,uniform bool localOnly,out float4 prelitGI) {
    float3 n=smoothNormal.xyz;
    float4 probe=probeIrradiance(p+n*.25,n,localOnly);
    float3 ambient=max(AmbientLight.rgb,0);
    float3 measured=probe.rgb/3.14159265;
    float3 radiance=measured*max(1+GIResponse.y,0);
    [branch]if(!localOnly&&PassInfo.z==4){
        prelitGI=0;
        return radiance*(probe.a*GridInfo.y*max(1+AmbientLight.w,0));
    }
    // Clamp before residency fade: strong GI cannot remove more than ambient,
    // and a missing/arriving probe never changes the native fallback.
    // The CPU environment emits from the upper world hemisphere only. Outside
    // the finite probe window, a vertical wall receives half that sky term,
    // not the horizontal surface's full ambient. Valid probes replace this
    // analytic fallback, including their occlusion and measured bounce.
    // A one-sided normal still describes a receiver. Confidence may suppress
    // added bounce, but must not restore full ambient along every facet seam.
    float skyView=saturate(.5+.5*n.z);
    float3 fallback=max(ambient*(skyView-1)*GridInfo.y,-ambient);
    float3 measuredCorrection=max((radiance-ambient)*GridInfo.y,-ambient);
    float3 correction=lerp(fallback,measuredCorrection,probe.a);
    // Confidence describes the depth-derived NORMAL, not probe coverage.
    // At a wall/ground seam a valid dark probe must still remove native ambient.
    // Only uncertain added bounce is suppressed; ambient removal also applies
    // to wall silhouettes whose depth-derived normal is uncertain.
    correction=min(correction,0)+max(correction,0)*smoothNormal.w;
    // Boost only the resulting ambient + probe/bounce estimate once, preserving
    // its hue and existing occlusion. No absolute brightness floor, direct-light
    // multiplier or brightened sky/fog.
    correction+=max(AmbientLight.rgb+correction,0)*AmbientLight.w;
    // Resident probes also constrain authored WMO illumination. Missing probes
    // retain it; a measured dark room must not inherit the outdoor ambient.
    // Brightness adjusts added indirect light, not geometric occlusion.
    // In particular -100% must not turn every resident room probe opaque.
    float occlusion=saturate(1-dot(measured,float3(.2126,.7152,.0722))/max(dot(ambient,float3(.2126,.7152,.0722)),.0001));
    prelitGI=float4(max(correction,0),occlusion)*(probe.a*GIResponse.x);
    return correction;
}
LightingOutput worldLightingValue(float2 uv,bool soft,bool shadows,bool gi=false,bool separateActors=true) {
    float2 halfUV=uv;
    uv=halfDepthUV(uv);float d=normalizedDepth(uv);
    LightingOutput o;o.correction=float4(0,0,0,SourcePolicy.y);o.baseline=float4(max(AmbientLight.rgb,.15),1);o.daylight=0;
    if(d>=.99999||waterDistance(uv,d)>0)return o;
    float4 smoothNormal=tex2Dlod(NormalBuffer,float4(halfUV,0,0));
    float3 p=worldPosition(uv,d);float3 n=smoothNormal.xyz;
    float4 prelitGI=0;float3 indirect=0;
    if(gi)indirect=worldGIValue(p,smoothNormal,true,prelitGI);
    // Fine material light is applied after the half-res spatial/temporal filters.
    // The legacy route remains available when there is no independent scene copy.
    float3 detailNormal=n;
    if(BumpControl.z<.5)detailNormal=autoBumpNormal(uv,p,n,viewDistance(d),InverseView[0].xyz,InverseView[1].xyz,InverseView[2].xyz);
    float shadow=1;
    // 0.3.209 (task 29): the moving casters near the player shadow from the actor map (ActorShadow, written per source at this
    // pass's half-res grid; white when off); the near cascade skips those, so take the darker of both.
    if(shadows&&DirectLight.w>.5&&dot(DirectLight.rgb,1)>0){
        if(soft){float2 v=tex2Dlod(DirectionalVisibility,float4(halfUV,0,0)).rg;shadow=SunDirection.w>.5?v.g:v.r;}
        else {float4 receiver=shadowReceiver(halfUV,d,smoothNormal);
            shadow=directionalShadowFiltered(p+n*(.08*saturate(receiver.w)),n,saturate(receiver.w),false,receiver.w);}
        if(separateActors){float2 actor=tex2Dlod(ActorVisibility,float4(halfUV,0,0)).rg;shadow=min(shadow,SunDirection.w>.5?actor.g:actor.r);}}
    shadow=1-saturate((1-shadow)*shadowParameters().x);
    float visibility=lerp(1,shadow,GridInfo.z);
    // Sun retains the authored angular response, with the regional light color.
    // Moon replaces its share of
    // that response with light from the visible moon, using the smoothed normal.
    // Subtract the old share first: an additive-only moon would double-count
    // native direct light. A fully occluded moon contributes zero new light.
    float3 old=LegacyDirect.rgb*saturate(dot(n,LegacyDirection.xyz));
    float3 painted=old*SourcePolicy.y;
    // 0.3.198 (rain): rain softens the DIRECT shadowing only (z = the share removed; 0 = exactly as before: the branch is not taken). The alpha
    // below, the baseline alpha and GridInfo.z (a divisor elsewhere) keep the true visibility, so the removal and the lamps see the real shadow.
    float moonShadow=shadow,sunVisibility=visibility;
    if(shadows&&WeatherInfo.z>0){moonShadow=lerp(1,shadow,1-WeatherInfo.z);sunVisibility=lerp(1,shadow,GridInfo.z*(1-WeatherInfo.z));}
    float3 moon=DirectLight.rgb*saturate(dot(detailNormal,SunDirection.xyz))*moonShadow;
    float3 sun=DirectLight.rgb*saturate(dot(detailNormal,LegacyDirection.xyz))*sunVisibility;
    float3 replacement=lerp(sun,moon,SunDirection.w);
    o.correction=float4(replacement-painted,lerp(visibility,shadow,SunDirection.w)*SourcePolicy.y);
    // Mode 2 isolates GI: the shared buffer must contain no direct correction.
    if(PassInfo.z==2)o.correction.rgb=0;
    o.correction.rgb+=indirect;
    // Share visibility with LocalDirect. Unknown coverage is -1 and must not
    // dim lamps as if this receiver had proven direct daylight.
    float3 cover=affinePoint(p,FarMatrix);
    float known=max(abs(cover.x),abs(cover.y))<=1&&cover.z>=0&&cover.z<=1?1:0;
    o.baseline=float4(max(old+AmbientLight.rgb,.15),known?lerp(visibility,shadow,SunDirection.w):-1);
    // Prelit WMO has no native sunlight to subtract. Publish incoming daylight
    // separately from the outdoor correction and GI's ambient replacement.
    // Uncovered receivers retain authored lighting rather than leaking sun.
    o.daylight=float4(DirectLight.rgb*saturate(dot(detailNormal,SunDirection.xyz))*shadow*known*DirectLight.w,0)+prelitGI;
    return o;
}
LightingOutput WorldLighting(float2 uv:TEXCOORD0){return worldLightingValue(uv,false,true);}
LightingOutput WorldLightingSoft(float2 uv:TEXCOORD0){return worldLightingValue(uv,true,true);}
LightingOutput WorldLightingShared(float2 uv:TEXCOORD0){return worldLightingValue(uv,true,true,false,false);}
// Disabled shadows bind neutral maps: keep the same relighting and baseline,
// without filtering an all-lit map or reading the neutral actor visibility.
LightingOutput WorldLightingUnshadowed(float2 uv:TEXCOORD0){return worldLightingValue(uv,false,false);}
LightingOutput WorldLightingUnshadowedGI(float2 uv:TEXCOORD0){return worldLightingValue(uv,false,false,true);}
