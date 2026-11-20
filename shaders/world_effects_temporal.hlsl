// 0.3.159 shadow removal smoothing (always on). In sun shadow the correction removes the game's
// painted sun light in proportion to N.L of the depth-derived FACET normal, but the
// game painted it with smooth vertex normals: the removal steps at every facet crease
// and at the wide/one-pixel normal switch, leaving jagged patches of the game's own
// highlight. Average the removal the composite will apply, as a fraction of the
// transported colour, over a 1.5-unit disc: the centre and 24 golden-angle taps on
// the same surface (depth) in the same shadow state (visibility). Lit pixels keep
// their value. The fraction includes the composite's albedo bound (legacy fog T):
// correction/baseline alone still steps where the facet normal predicts no painted
// light, because the albedo estimate saturates there.
// 0.3.185 direction-dependent halo: looking toward a low sun, the camera-facing face of an actor or
// cliff faces away from it (N.L<=0, removal ratio about 0) in the same shadow state as the ground, and
// the depth weight alone accepts it at the contact: its zero removal leaked into the adjacent ground
// (under-removal, a light halo). Each tap is also weighted by the agreement of the smoothed world
// normal (s14): full up to 35 degrees, none from 50. A wall is ~90 degrees from the ground at any
// distance, which no depth tolerance can tell. 0.3.186: was 25/40, which rejected taps across
// low-poly dune facets (grazing creases of 35-45 degrees whose N.L removal steps the smoothing hides,
// under a low sun or the moon) and brought back faint sharp triangles; creases up to 35 degrees now
// smooth exactly as without the test. A 45 degree crease and a 45 degree face look the same to any
// angle gate, so at 45 both are half-way (face halo 4.8/255 at 25 u, 8.5 untested; crease +4.3/255
// over untested); 55 and the vertical faces are rejected. Source-aware variants (reject only taps
// facing away from the source) changed nothing: the away-facing dune facet is that very case. A
// silhouette centre (confidence 0: its one-sided normal may be the neighbour's) skips the test (the old weights); silhouette taps of
// a confident centre are tested with their own normal. This filter is shared by RemovalSmooth
// and TemporalLightMerged. The separate path writes SmoothedLighting (s9); both temporal
// variants clamp against the raw s8.
// The composite applies correction*min(transported/baseline,T); as a fraction of the
// transported colour that is correction*T/removalScale.
// legacyT>=.001 keeps the first operand >=.00015, so the scene term needs no floor.
float3 removalScale(float3 baseline,float3 scene,float3 fog,float legacyT){
    return max(legacyT*max(baseline,.15),scene-min(fog,scene));
}
// The probe atlas already blends its publications in world space. In a full
// directional shadow its correction belongs to this receiver's native baseline.
// Borrowing another receiver's signed RGB there creates coloured bands even
// with identical shadow visibility. Ease the filter back in across a penumbra.
float directionalFilterWeight(float visibility){
    return GridInfo.y>0&&RemovalInfo.y>0?saturate(32*visibility*RemovalInfo.z):1;
}
bool temporalWater(float2 uv,float z){
    [branch]if(RemovalInfo.x<.5)return false;
    float2 m=tex2Dlod(WaterMask,float4(uv,0,0)).rg;
    return m.x>ImageClip.z&&m.x<=z+max(.012,m.x*.0007)&&m.y>.003;
}
// A projected plane is affine in device depth. Only smooth where its slope
// changes, using the same four neighbours for the test and the filter.
float3 indirectEdgeDelta(float4 current,float2 q,float2 size,float2 uv,float d,float z,bool merged){
    float2 stepUV=2*ImageClip.xy;
    float2 dx=float2(stepUV.x,0),dy=float2(0,stepUV.y);
    float4 depths=float4(tex2Dlod(Depth,float4(uv+dx,0,0)).r,tex2Dlod(Depth,float4(uv+dy,0,0)).r,
                         tex2Dlod(Depth,float4(uv-dx,0,0)).r,tex2Dlod(Depth,float4(uv-dy,0,0)).r);
    depths=saturate((depths-Projection.w)*DepthParams.x);
    float2 bend=abs((depths.xy-d)+(depths.zw-d));
    float crease=saturate(max(bend.x,bend.y)/max(2e-7,.00015*ImageClip.z/z)-1);
    [branch]if(crease<=0||temporalWater(uv,z))return 0;
    // Compare corrections relative to their own receivers' baselines. Reject
    // gaps in depth, water and changes in direct visibility.
    if(!merged)current=tex2Dlod(LightingBuffer,float4(q,0,0));
    float3 scale=max(tex2Dlod(BaselineLighting,float4(q,0,0)).rgb,.15);
    float3 center=current.rgb/scale;
    float3 delta=0;float total=.5;
    float depthScale=2/max(.075,z*.012);
    float2 offset=float2(1,0);
    [loop]for(int i=0;i<4;++i){
        float tapZ=viewDistance(depths.x);
        float weight=saturate(2-abs(tapZ-z)*depthScale);
        [branch]if(weight>0&&depths.x<.99999&&!temporalWater(uv+offset*stepUV,tapZ)){
            float2 tq=q+offset/size;
            float4 light=tex2Dlod(LightingBuffer,float4(tq,0,0));
            weight*=saturate(1-32*abs(light.a-current.a)*RemovalInfo.z);
            [branch]if(weight>0){
                float3 otherScale=max(tex2Dlod(BaselineLighting,float4(tq,0,0)).rgb,.15);
                delta+=weight*(light.rgb/otherScale-center);
                total+=weight;
            }
        }
        offset=float2(-offset.y,offset.x);
        depths=depths.yzwx;
    }
    return scale*(delta*(crease/total));
}
float3 smoothRemoval(float4 current,float2 q,float2 base,float2 size,float z,bool skipZeroTaps=false){
    float amount=saturate((1-current.a*RemovalInfo.z)/GridInfo.z)*directionalFilterWeight(current.a);
    float3 result=current.rgb;
    // Fractional source weights leave fp16 alpha a hair below 1/RemovalInfo.z in sunlight: skip
    // the taps below 1/256, which also keeps those lit pixels exactly unchanged.
    [branch]if(amount>1.0/256){
        float legacyT=max(mad(LegacyFog.w,saturate(pow(max(mad(z*Projection.z,LegacyFog.x,LegacyFog.y),0),LegacyFog.z))-1,1),.001);
        float3 fog=(1-legacyT)*LegacyFogColor.rgb;
        // 0.3.174: the Scene copy precedes the contact AO when the world composites; apply it here
        // (a neutral 1 otherwise) so the removal sees the colour the composite relights.
        float3 scale=removalScale(tex2Dlod(BaselineLighting,float4(q,0,0)).rgb,tex2Dlod(Scene,float4(q,0,0)).rgb*tex2Dlod(AmbientOcclusion,float4(q,0,0)).a,fog,legacyT);
        float3 ratio=current.rgb*legacyT/scale;
        float radius=clamp(RemovalInfo.w/z,3,16);
        float depthScale=-1.442695/max(.25,z*.03);
        float visibilityScale=32*RemovalInfo.z/GridInfo.z;
        float4 centreNormal=tex2Dlod(NormalBuffer,float4(q,0,0));
        float3 sum=max(ratio,-shadowRemovalLimit());float total=1,t=.5;float2 dir=float2(1,0);
        // Same spiral and accumulation order; two taps per loop control step.
        [loop]for(int group=0;group<12;++group){
        [unroll]for(int sub=0;sub<2;++sub){
            float2 tq=(clamp(floor(base+.5+dir*(sqrt(t*(1.0/24))*radius)),0,size-1)+.5)/size;
            float4 light=tex2Dlod(LightingBuffer,float4(tq,0,0));
            float4 tapNormal=tex2Dlod(NormalBuffer,float4(tq,0,0));
            float agree=saturate((dot(centreNormal.xyz,tapNormal.xyz)-.6428)*5.67);
            float w=exp2(abs(tex2Dlod(CurrentDistance,float4(tq,0,0)).r-z)*depthScale)*saturate(1-abs(light.a-current.a)*visibilityScale)*(centreNormal.w>.5?agree:1);
            // Zero-weight taps cannot contribute. Skip their scene/AO/baseline reads;
            // retain every accepted tap and the original accumulation order.
            if(skipZeroTaps){
                [branch]if(w>0)sum+=max(light.rgb*legacyT/removalScale(tex2Dlod(BaselineLighting,float4(tq,0,0)).rgb,tex2Dlod(Scene,float4(tq,0,0)).rgb*tex2Dlod(AmbientOcclusion,float4(tq,0,0)).a,fog,legacyT),-shadowRemovalLimit())*w;
            }else{
                [flatten]if(w>0)sum+=max(light.rgb*legacyT/removalScale(tex2Dlod(BaselineLighting,float4(tq,0,0)).rgb,tex2Dlod(Scene,float4(tq,0,0)).rgb*tex2Dlod(AmbientOcclusion,float4(tq,0,0)).a,fog,legacyT),-shadowRemovalLimit())*w;
            }
            total+=w;
            dir=float2(dir.x*-.7373688-dir.y*.6754903,dir.x*.6754903-dir.y*.7373688);t+=1;
        }
        }
        result=lerp(ratio,sum/total,amount)*scale/legacyT;
    }
    return result;
}
// Separate smoothing remains the diagnostic and device-failure fallback.
// Normal frames fuse the same filter into TemporalLightMerged; no taps are removed.
float4 RemovalSmooth(float2 uv:TEXCOORD0):COLOR0 {
    float2 half=ScreenSize.zw;
    float2 base=floor(uv*half);float2 q=(base+.5)/half;
    float4 current=tex2Dlod(LightingBuffer,float4(q,0,0));
    float d=normalizedDepth(halfDepthTexelUV(base));
    [branch]if(RemovalInfo.y>0&&d<.99999)current.rgb=smoothRemoval(current,q,base,half,viewDistance(d));
    return current;
}
// Temporal stabilization of the half-resolution lighting buffer (shadow
// visibility, GI and local light corrections). The previous frame's result is
// reprojected through the previous view, accepted only where its stored view
// distance agrees with this frame's surface, and clamped to the current 3x3
// cross neighbourhood so moving casters leave no trail. Removes per-frame
// flicker from depth-derived normals, caster set changes and MSAA edges.
struct TemporalOutput {float4 light:COLOR0;float4 depth:COLOR1;};
// Same dry distance and half-grid centres as TemporalLight, without lighting inputs.
float4 WorldDistance(float2 uv:TEXCOORD0):COLOR0 {
    return float4(viewDistance(normalizedDepth(halfDepthUV(uv))),0,0,1);
}
TemporalOutput temporalLight(float2 uv,bool merged,sampler2D historyDepth,sampler2D historyLight,bool skipZeroTaps=false) {
    TemporalOutput o;float2 half=ScreenSize.zw;
    float2 base=floor(uv*half);float2 q=(base+.5)/half;
    float4 current=merged?tex2Dlod(LightingBuffer,float4(q,0,0)):tex2Dlod(SmoothedLighting,float4(q,0,0));
    float2 duv=halfDepthTexelUV(base);float d=normalizedDepth(duv);float z=viewDistance(d);
    float directFilter=directionalFilterWeight(current.a);
    float3 indirect=0;
    [branch]if(directFilter<1&&d<.99999&&all(base>0)&&all(base<half-1)){
        indirect=(1-directFilter)*indirectEdgeDelta(current,q,half,duv,d,z,merged);
    }
    [branch]if(merged&&RemovalInfo.y>0&&d<.99999)current.rgb=smoothRemoval(current,q,base,half,z,skipZeroTaps);
    current.rgb+=indirect;
    o.light=current;o.depth=float4(z,0,0,1);
    float historyWeight=TemporalInfo.x*directFilter;
    if(historyWeight<=0||d>=.99999)return o;
    float3 p=worldPosition(duv,d);
    float3 v=affinePoint(p,PreviousView);float w=v.z*Projection.z;
    if(w<=ImageClip.z)return o;
    float2 puv=(v.xy*Projection.xy/w)*float2(.5,-.5)+.5;
    if(any(puv<0)||any(puv>1))return o;
    float2 pq=(clamp(floor(puv*half),0,half-1)+.5)/half;
    float hz=tex2Dlod(historyDepth,float4(pq,0,0)).r;
    if(abs(hz-w)>max(.25,w*.03))return o;
    // Bilinear light history (s14 separately, s9 in the merged pass). The nearest history texel drifted
    // up to .75 half-res px while moving (sign flipping with the flow's fraction: waves on
    // static edges). A per-tap weighted depth test does not fit the slot budget (515): read
    // bilinearly only where all four footprint depths agree with this surface; elsewhere
    // (silhouettes) read the nearest texel's centre, which LINEAR returns unmixed, as before.
    float tol=max(.25,w*.03);
    float2 texel=1/half,corner=(floor(puv*half-.5)+.5)*texel; // CLAMP addressing repeats the border
    float4 footprint=float4(tex2Dlod(historyDepth,float4(corner,0,0)).r,
        tex2Dlod(historyDepth,float4(corner+float2(texel.x,0),0,0)).r,
        tex2Dlod(historyDepth,float4(corner+float2(0,texel.y),0,0)).r,
        tex2Dlod(historyDepth,float4(corner+texel,0,0)).r);
    bool agree=all(abs(footprint-w)<=tol);
    float4 history=tex2Dlod(historyLight,float4(agree?puv:pq,0,0));
    float4 lo=current,hi=current;
    [unroll]for(int i=0;i<4;++i){
        float2 offset=float2(i==0?-1:(i==1?1:0),i==2?-1:(i==3?1:0));
        float4 s=tex2Dlod(LightingBuffer,float4((clamp(base+offset,0,half-1)+.5)/half,0,0));
        lo=min(lo,s);hi=max(hi,s);
    }
    // 0.3.170: shadow texel flips of an animated caster (idle pose) change visibility
    // by up to 15% (near 5x5 tent) every frame, in step with the whole neighbourhood,
    // so the tight clamp kept none of the history and each flip popped. With a still
    // camera (reprojection within .25 half-res pixel, none from .5; 0.3.171) let the visibility
    // (alpha) history stray up to .15 of full visibility outside the neighbourhood, and
    // the rgb by the direct light that change can carry (TemporalReach.w: the largest
    // drawn source channel per unit of normalised visibility). History visibility
    // inside the neighbourhood: exactly the tight clamp.
    float4 tight=clamp(history,lo,hi);
    float still=saturate(2-4*length(puv*half-(base+.5)));
    float margin=.15/max(RemovalInfo.z,1e-4)*RemovalInfo.y*still;
    float loose=clamp(history.a,lo.a-margin,hi.a+margin);
    float3 reach=abs(loose-tight.a)*RemovalInfo.z*TemporalReach.w;
    o.light=lerp(current,float4(clamp(history.rgb,lo.rgb-reach,hi.rgb+reach),loose),historyWeight);
    return o;
}
// Keep current normals/distance at s14/s15 for smoothing; histories use idle slots.
sampler2D MergedLightHistory:register(s9);
sampler2D MergedDepthHistory:register(s13);
TemporalOutput TemporalLight(float2 uv:TEXCOORD0){return temporalLight(uv,false,DepthHistory,LightHistory);}
TemporalOutput TemporalLightMerged(float2 uv:TEXCOORD0){return temporalLight(uv,true,MergedDepthHistory,MergedLightHistory);}
TemporalOutput TemporalLightMergedZeroWeight(float2 uv:TEXCOORD0){return temporalLight(uv,true,MergedDepthHistory,MergedLightHistory,true);}
// 1x1 pass: fraction of the source disc (on screen) that is clear sky, from
// 16 depth taps in a 4x4 grid over the disc, or .6 x the clear fraction of a ring
// around it when larger (r43 wrap), blended with last frame's value.
// Off-screen or behind-camera sources count as fully visible (unknown).
float4 SourceVisibilityPS(float2 uv:TEXCOORD0):COLOR0 {
    float previous=saturate(tex2Dlod(PreviousSourceVisibility,float4(.5,.5,0,0)).r);
    float3 sv=float3(dot(SunDirection.xyz,InverseView[0].xyz),dot(SunDirection.xyz,InverseView[1].xyz),dot(SunDirection.xyz,InverseView[2].xyz));
    float sw=sv.z*Projection.z;
    float current=1;
    if(sw>.05){
        float2 suv=(sv.xy*Projection.xy/sw)*float2(.5,-.5)+.5;
        if(all(suv>0)&&all(suv<1)){
            // Disc radius on screen in uv units: tangent radius through the projection.
            float2 r=SourceInfo.x*Projection.xy*.5;
            float clear=0;
            [unroll]for(int i=0;i<16;++i){
                float2 offset=float2((i%4)-1.5,(i/4)-1.5)*(2.0/3.0)*r;
                float2 q=saturate(suv+offset);
                clear+=(tex2Dlod(Depth,float4(q,0,0)).r-Projection.w)*DepthParams.x>=.99999?1:0;
            }
            current=clear*(1.0/16);
            // r43 wrap: 8 taps on a ring at 2.5/3.5 disc radii keep part of the aureole
            // when only the disc is covered (a trunk in front of the sun). Off-screen ring
            // taps do not count; with none on screen the disc value stands.
            float ringClear=0,ringTaps=0;
            [unroll]for(int j=0;j<8;++j){
                float2 q=suv+float2(cos(j*.78539816),sin(j*.78539816))*((j%2)?3.5:2.5)*r;
                float inside=step(abs(q.x-.5),.5)*step(abs(q.y-.5),.5);
                ringTaps+=inside;
                ringClear+=inside*step(.99999,(tex2Dlod(Depth,float4(q,0,0)).r-Projection.w)*DepthParams.x);
            }
            current=max(current,.6*ringClear/max(ringTaps,1));
        }
    }
    return lerp(current,previous,SourceInfo.y).xxxx;
}
