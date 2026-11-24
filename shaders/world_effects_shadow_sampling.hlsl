// Shared by directional, actor, transition and composite passes. w validates
// the optional bank for offline/legacy callers that only upload c0..c67.
float4 ShadowTuning : register(c115);
float3 shadowParameters(){return ShadowTuning.w>.5?ShadowTuning.xyz:float3(1,.5,1);}
// The shared lighting buffer includes ambient replacement. A direct-shadow
// art limit must not restore native ambient inside a measured dark room.
// GridInfo.y is zero when the GI pass is disabled, including the live toggle.
float directRemovalLimit(){return .45+.4*saturate(shadowParameters().x-1);}
float shadowRemovalLimit(){return max(directRemovalLimit(),saturate(GridInfo.y));}

// Shading confidence is not geometric validity: a selected one-sided normal
// still needs a receiver plane. Removing its plane/lift creates acne at joins.
// At a crease the filtered shading plane spans two facets. Enlarge only that
// receiver's depth allowance, bounded by three existing receiver offsets.
// Reject sky and depth breaks so an unrelated silhouette cannot set the scale.
float4 shadowReceiver(float2 screen,float depth,float4 normal) {
    float bend=0,z=viewDistance(depth);float2 center=halfDepthUV(screen);
    [loop]for(int k=0;k<4;++k){
        float2 step=k<2?float2((k==0?-1:1)/ScreenSize.z,0):float2(0,(k==2?-1:1)/ScreenSize.w);
        float4 other=tex2Dlod(NormalBuffer,float4(screen+step,0,0));
        float otherZ=viewDistance(normalizedDepth(center+step));
        float3 delta=normal.xyz-other.xyz;
        if(other.w<=1&&abs(otherZ-z)<max(.05,z*.01))bend=max(bend,dot(delta,delta));
    }
    float valid=dot(normal.xyz,normal.xyz)>.5?1:0;
    return float4(normal.xyz,valid*(1+3*saturate(4*sqrt(bend))));
}
float shadowReceiverAllowance(float confidence,float scale,float lift) {
    // Extra allowance follows the light ray. Enlarging the normal displacement
    // crosses the opposite facet of a concave join and creates a second seam.
    return (scale-1)*confidence*(.08+lift)*ShadowRange.x;
}
float shadowReceiverFacing(float facing,float confidence){
    // PCF can see past a silhouette on a face turned away from this light.
    // The native sun's angular response uses a different direction, so that
    // leaked visibility remains visible as bright teeth on a shaded cliff.
    return saturate(1+32*facing*confidence);
}

float shadowDepth(float2 uv,bool useNear) {
    if(useNear)return tex2Dlod(ShadowNear,float4(uv,0,0)).r;
    return tex2Dlod(ShadowFar,float4(uv,0,0)).r;
}
float shadowMapFiltered(float3 lp,bool useNear,float radius,float2 gradient,bool soft) {
    // Raster NDC maps to integer D3D9 pixel centers. Sampling the resulting
    // texture requires +.5 texel; this also aligns receiver-plane PCF offsets.
    float2 uv=float2(lp.x*.5+.5,.5-lp.y*.5)+.5*DepthParams.w;
    if(any(uv<.003)||any(uv>.997)||lp.z<=0||lp.z>=1)return 1;
    // The far cascade texel covers four times the world distance of the near
    // one, so its slope error is four times larger: scale the bias with it.
    // Constant bias shifts a shadow edge on the ground by bias/tan(elevation),
    // and a bias DIFFERENCE between cascades makes shadows jump where the eye
    // centred cascades hand over while the camera orbits. Keep both small
    // (near 0.05 u, far 0.1 u) and let the receiver plane absorb slopes.
    float receiver=lp.z-PassInfo.x*(useNear?1:2);
    // Depth-derived normals are noisy on foliage. Limit the receiver-plane
    // slope so one flipped normal cannot swing every expected depth, but
    // keep genuine grazing terrain inside the limit down to a 2-degree light
    // incidence (below that saturate(n.L) makes the shadow term negligible).
    // Same world-space slope limit for either depth span; XY texels stay
    // near .094 u / far .375 u. Only normalized depth units change.
    float limit=57.3*(useNear?DepthParams.y:DepthParams.z)*ShadowRange.x;
    gradient=clamp(gradient,-limit,limit);
    // Compact contact support keeps leaf coverage; PCSS widens only the near
    // map. The far map has a fixed sample budget regardless of world reach.
    float reach=shadowParameters().z;
    float2 pixel=uv/DepthParams.w-.5;
    float2 base=floor(pixel-reach)+1,fraction=pixel-base;
    float size=ceil(2*reach),start=0;
    float visibility=0,total=0,gaps=0,blockers=0;
    // The exit lives in the loop condition: Microsoft's compiler turned the nested bare break into break_ne with identical operands.
    [loop]for(int y=0;y<6&&y<size;++y){
    float rowOffset=y+start,rowUV=(base.y+rowOffset+.5)*DepthParams.w;
    float rowWeight=max(reach-abs(rowOffset-fraction.y),0);
    [loop]for(int x=0;x<6&&x<size;++x){
        float offset=x+start;
        float2 tapUV=float2((base.x+offset+.5)*DepthParams.w,rowUV);
        float w=max(reach-abs(offset-fraction.x),0)*rowWeight;
        // Compare each sample against the depth of the receiver's plane there.
        float expected=receiver+dot(gradient,tapUV-uv);
        float stored=shadowDepth(tapUV,useNear);
        visibility+=(expected<=stored?1:0)*w;total+=w;
        if(soft&&stored<expected){gaps+=expected-stored;++blockers;}
    }
    }
    if(soft&&useNear&&shadowParameters().y>0){
        // Keep every contact tap above, then search the wider support. The
        // separation uses the receiver plane, so slopes do not create softness.
        const float widest=4.25;
        [loop]for(int by=0;by<4;++by)[loop]for(int bx=0;bx<4;++bx){
            float2 tap=floor(pixel+(float2(bx,by)-1.5)*(widest/1.5)+.5);
            float2 tapUV=(tap+.5)*DepthParams.w;
            float expected=receiver+dot(gradient,tapUV-uv),stored=shadowDepth(tapUV,useNear);
            if(stored<expected){gaps+=expected-stored;++blockers;}
        }
        float texelWorld=2*radius*DepthParams.w;
        float softReach=clamp(.04*shadowParameters().y*gaps/max(blockers,1)/max(ShadowRange.x*texelWorld,1e-8),reach,widest);
        // No extra filtering at contact. A sparse search miss retains the
        // complete base PCF instead of erasing thin geometry.
        if(softReach>reach+.001){
            float2 first=floor(pixel-softReach)+1;
            float extent=floor(2*softReach)+1;visibility=0;total=0;
            [loop]for(int sy=0;sy<9&&sy<extent;++sy){
            float rowTap=first.y+sy,rowUV=(rowTap+.5)*DepthParams.w;
            float rowWeight=max(softReach-abs(rowTap-pixel.y),0);
            [loop]for(int sx=0;sx<9&&sx<extent;++sx){
                float2 tap=float2(first.x+sx,rowTap),tapUV=float2((tap.x+.5)*DepthParams.w,rowUV);
                float w=max(softReach-abs(tap.x-pixel.x),0)*rowWeight;
                float expected=receiver+dot(gradient,tapUV-uv);
                visibility+=(expected<=shadowDepth(tapUV,useNear)?1:0)*w;total+=w;
            }
            }
        }
    }
    float edge=saturate((1-max(abs(lp.x),abs(lp.y)))/.12);
    return lerp(1,visibility/max(total,.0001),edge);
}
float shadowMap(float3 lp,bool useNear,float radius,float2 gradient){return shadowMapFiltered(lp,useNear,radius,gradient,false);}
float shadowNearBlend(float3 q){return q.z<=0||q.z>=1?1:saturate((max(abs(q.x),abs(q.y))-.72)/.18);}
float directionalShadowFiltered(float3 p,float3 normal,float confidence,bool soft,float receiverScale=1) {
    float facing=dot(normal,SunDirection.xyz);
    // Normal-offset per cascade: at grazing incidence (low sun on flat sand)
    // one far texel (.375 u) spans over a unit of light depth, so any error
    // in the receiver plane exceeds the .1 u constant bias and the far
    // cascade shades the whole plain, with a front that follows the pivot.
    // Lifting the receiver along its normal by one texel times (1-|n.l|)
    // removes that without a larger constant depth bias. The caller separates
    // geometric validity from the shading-confidence flag at a silhouette.
    float lift=(1-saturate(abs(facing)))*confidence*2/1024;
    float3 q=affinePoint(p+normal*(lift*DepthParams.y),NearMatrix),farQ=affinePoint(p+normal*(lift*DepthParams.z),FarMatrix);
    q.z-=shadowReceiverAllowance(confidence,receiverScale,lift*DepthParams.y);
    farQ.z-=shadowReceiverAllowance(confidence,receiverScale,lift*DepthParams.z);
    float blend=shadowNearBlend(q);
    // Matrix columns contain light basis vectors divided by cascade radius.
    float2 plane=float2(dot(normal,float3(NearMatrix[0].x,NearMatrix[1].x,NearMatrix[2].x)),
                        -dot(normal,float3(NearMatrix[0].y,NearMatrix[1].y,NearMatrix[2].y)))*DepthParams.y;
    plane*=confidence*2*ShadowRange.x/(facing<0?-max(abs(facing),.15):max(abs(facing),.15));
    float result=0;
    [loop]for(int cascade=0;cascade<2;++cascade) {
        bool useNear=cascade==0;float radius=useNear?DepthParams.y:DepthParams.z;
        float weight=useNear?1-blend:blend;
        if(weight>0){
            float visibility=shadowMapFiltered(useNear?q:farQ,useNear,radius,plane*radius,soft);
            result+=visibility*weight;
        }
    }
    return result*shadowReceiverFacing(facing,confidence);
}
float directionalShadow(float3 p,float3 normal,float confidence){return directionalShadowFiltered(p,normal,confidence,false);}
float4 DirectionalShadow(float2 uv:TEXCOORD0):COLOR0 {
    float2 screen=uv;uv=halfDepthUV(uv);float depth=normalizedDepth(uv);
    if(depth>=.99999||waterDistance(uv,depth)>0)return 1;
    float4 normal=shadowReceiver(screen,depth,tex2Dlod(NormalBuffer,float4(screen,0,0)));
    float3 p=worldPosition(uv,depth)+normal.xyz*(.08*saturate(normal.w));
    float v=directionalShadowFiltered(p,normal.xyz,saturate(normal.w),true,normal.w);
    return float4(v,v,v,1);
}
#ifndef NORTHLIGHT_SHADOW_TRANSITION
float fogShadowProjected(float3 q) {
    // The 192-unit far map covers the 128-unit air volume. Use it throughout:
    // switching cascade resolution around the moving player changes thin
    // foliage visibility even at an otherwise stationary world sample.
    float2 uv=float2(q.x*.5+.5,.5-q.y*.5)+.5*DepthParams.w;
    float2 edge=min(uv,1-uv);
    if(min(min(edge.x,edge.y),min(q.z,1-q.z))<0)return 1;
    // Filter on a 1.5-world-unit light-plane lattice, independent of the shadow
    // camera/pivot. Fine leaf silhouettes otherwise alias into falling streaks
    // when the view-ray samples move. Interpolate comparisons, never depths.
    float2 plane=(q.xy-FarMatrix[3].xy)*float2(DepthParams.z,-DepthParams.z)/1.5;
    float2 fraction=frac(plane);
    float2 weight=fraction*fraction*(3-2*fraction);
    float receiver=q.z-PassInfo.x*4,visibility=0;
    [unroll]for(int tap=0;tap<4;++tap){
        float row=floor(tap*.5);float2 bit=float2(tap-row*2,row);
        float2 sampleUV=uv+(bit-fraction)*(1.5/(2*DepthParams.z));
        float z=tex2Dlod(ShadowFar,float4(sampleUV,0,0)).r;
        float2 w=lerp(1-weight,weight,bit);
        visibility+=(receiver<=z?1:0)*w.x*w.y;
    }
    return visibility;
}
#else
float fogShadowProjected(float3 q);
#endif
// Reuse the already-rendered fine actor map; never replay geometry for fog.
// The far map continues to provide terrain, WMO and static-prop silhouettes.
sampler2D FogActorMap : register(s4);
sampler2D FogActorTiles : register(s5);
row_major float4x4 FogActorMatrix : register(c68);
float4 FogActorBounds : register(c66); // current map's conservative caster coverage
float4 FogActorInfo : register(c72); // x: current source owns a valid actor map
// Exact line integral of point-sampled actor depth over this fog interval.
float fogActorShadow(float3 q,float3 delta){
    float3 inverse=(delta<0?-1:1)/max(abs(delta),1e-12);
    float3 a=(float3(FogActorBounds.xy,0)-q)*inverse;
    float3 b=(float3(FogActorBounds.zw,1)-q)*inverse;
    float3 enter=min(a,b),exit=max(a,b);
    float t=max(0,max(max(enter.x,enter.y),enter.z));
    float end=min(1,min(min(exit.x,exit.y),exit.z));
    if(t>=end)return 1;
    float2 origin=(q.xy*float2(.5,-.5)+.5)*1024+.5;
    float2 direction=delta.xy*float2(512,-512);
    float2 inv=(direction<0?-1:1)/max(abs(direction),1e-12);
    float2 cell=floor(origin+direction*t);
    float2 next=(cell+(direction>0?1:0)-origin)*inv;
    float covered=0;
    // SM3 loop counters execute at most 255 iterations. Nine bounded
    // blocks cover both 1024-texel axes plus entry/corner ties.
    [loop]for(int block=0;block<9&&t<end;++block)
    [loop]for(int i=0;i<228&&t<end;++i){
        float last=max(t,min(end,min(next.x,next.y)));
        float z=tex2Dlod(FogActorMap,float4((cell+.5)/1024,0,0)).r+PassInfo.x*4;
        float2 depth=q.z+delta.z*float2(t,last)-z;
        covered+=(last-t)*saturate(max(depth.x,depth.y)/max(abs(depth.y-depth.x),1e-20));
        float2 crossed=next<=last?1:0;
        cell+=sign(direction)*crossed;next+=abs(inv)*crossed;t=last;
    }
    return saturate(1-covered);
}

// Wide tile minima include eight fine texels around each block. Reject only
// intervals proved empty; possible casters keep the original fine integral.
float fogActorShadowTiled(float3 q,float3 delta,bool checkRange=false){
    float3 inverse=(delta<0?-1:1)/max(abs(delta),1e-12);
    float3 a=(float3(FogActorBounds.xy,0)-q)*inverse;
    float3 b=(float3(FogActorBounds.zw,1)-q)*inverse;
    float3 enter=min(a,b),exit=max(a,b);
    float t=max(0,max(max(enter.x,enter.y),enter.z));
    float end=min(1,min(min(exit.x,exit.y),exit.z));
    if(t>=end)return 1;
    float2 origin=(q.xy*float2(.5,-.5)+.5)*1024+.5;
    float2 direction=delta.xy*float2(512,-512);
    float2 inv=(direction<0?-1:1)/max(abs(direction),1e-12);
    float2 cell=floor((origin+direction*t)/16);
    float2 next=((cell+(direction>0?1:0))*16-origin)*inv;
    // Long imported intervals outside the proven coordinate domain use fine DDA.
    bool possible=checkRange&&(any(abs(q.xy)>32)||any(abs(q.xy+delta.xy)>32));
    [loop]for(int i=0;i<132&&!possible&&t<end;++i){
        float last=max(t,min(end,min(next.x,next.y)));
        float z=tex2Dlod(FogActorTiles,float4((cell+.5)/64,0,0)).r+PassInfo.x*4;
        float2 depth=q.z+delta.z*float2(t,last)-z;
        possible=max(depth.x,depth.y)>=-1e-5;
        float2 crossed=next<=last?1:0;
        cell+=sign(direction)*crossed;next+=abs(inv)*16*crossed;t=last;
    }
    if(possible||t<end)return fogActorShadow(q,delta);
    return 1;
}

// Cascades can contain the same caster. Compose visibility by union rather
// than multiplying the caster twice; an empty actor map preserves world bits.
float fogCombineActor(float world,float actor){return actor<1?min(world,actor):world;}

