// 0.3.209 (task 29): contact-hardening visibility of the moving casters (replays) in this source's actor map: 1024 texels over
// 2*ActorInfo.z units (.0234 u, four times finer than the near cascade), centred on the player. Drawn per source at
// WorldLighting's half-res grid into actorVisibility (the CPU masks the channel: r sun, g moon). Penumbra half width in world
// units grows with the blocker-receiver distance (PCSS): crisp at the feet, soft under a wing or a raised body.
static const float ActorPenumbraScale=.04; // half width per world unit between the average blocker and the receiver
static const float ActorPenumbraMin=.025;   // ~2 texels: contact shadows stay crisp without crawling at the half-res POINT read
static const float ActorPenumbraMax=.10;   // ~4.3 texels (at most 9x9 taps at a one-texel step); also the blocker search radius
static const float ActorTileTexels=16;   // actor texels per tile side (1024/16 = 64x64 tiles)
// ActorTiles: one 4x4 minimum step, drawn twice before each ActorShadow pass (the actor map 1024 -> 256 -> 64; ActorInfo.y is the
// source's texel in uv: 1/1024, then 1/256). The result is the nearest (minimum) depth of each 16x16 texel tile, so ActorShadow
// proves most receivers lit from four tile taps instead of its 41-tap blocker search. Two short steps instead of one 256-tap pass:
// 4096 pixels reading 256 texels each left the GPU waiting on serial fetches (~.15 ms). The map is cleared to 1: an empty tile reads 1.
float4 ActorTiles(float2 uv:TEXCOORD0,uniform float4 ActorInfo:register(c72)):COLOR0 {
    float2 base=floor(uv/(4*ActorInfo.y))*4+.5;
    float z=1;
    [unroll]for(int y=0;y<4;++y)[unroll]for(int x=0;x<4;++x)z=min(z,tex2Dlod(ActorMap,float4((base+float2(x,y))*ActorInfo.y,0,0)).r);
    return float4(z,z,z,1);
}
// Final 256 -> 64 reduction only: the border adds eight fine texels of
// conservative support. The same targets and point-sampled R32 depth are used.
float4 ActorTilesWide(float2 uv:TEXCOORD0,uniform float4 ActorInfo:register(c72)):COLOR0 {
    float2 base=floor(uv/(4*ActorInfo.y))*4+.5;
    float z=1;
    [unroll]for(int y=-2;y<6;++y)[unroll]for(int x=-2;x<6;++x)
        z=min(z,tex2Dlod(ActorMap,float4((base+float2(x,y))*ActorInfo.y,0,0)).r);
    return float4(z,z,z,1);
}
// ActorMatrix (c68..c71): this source's actor map matrix (layout as NearMatrix). ActorInfo (c72): x 1 = actor map valid for this
// source, y actor texel in uv (1/1024), z actor map radius (world units), w 0. Entry uniforms, not globals: a global would move
// every other entry's literal constants (def c68..).
float4 actorShadowValue(float2 uv,row_major float4x4 ActorMatrix,float4 ActorInfo,bool soft) {
    float2 screen=uv;
    uv=halfDepthUV(uv);float d=normalizedDepth(uv);
    if(ActorInfo.x<.5||d>=.99999)return 1;
    float3 position=worldPosition(uv,d);
    // Coarse border test first, so the pixels outside the map skip the water and normal fetches: the normal offset below moves the
    // point at most .04+2*texel*radius (~.064 u, .0053 of the 12 u half width), well inside the .01 margin.
    float3 coarse=affinePoint(position,ActorMatrix);
    if(max(abs(coarse.x),abs(coarse.y))>=1.01||waterDistance(uv,d)>0)return 1;
    float4 smoothNormal=tex2Dlod(NormalBuffer,float4(screen,0,0));
    float3 n=smoothNormal.xyz;float confidence=smoothNormal.w;
    float facing=dot(n,SunDirection.xyz);
    // directionalShadow's normal offset scaled to this map: half its .08 u lift (the texel is a quarter) plus one actor texel
    // times (1-|n.l|); a silhouette pixel's unreliable normal gets neither.
    float lift=.04+(1-saturate(abs(facing)))*2*ActorInfo.y*ActorInfo.z;
    float3 q=affinePoint(position+n*(lift*confidence),ActorMatrix);
    float border=max(abs(q.x),abs(q.y));
    if(border>=1||q.z<=0||q.z>=1)return 1;
    // Raster NDC maps to integer D3D9 pixel centers; sampling needs +.5 texel (as shadowMap).
    float2 mapUV=float2(q.x*.5+.5,.5-q.y*.5)+.5*ActorInfo.y;
    // The near cascade's depth bias: half of it let characters shadow themselves; the receiver plane absorbs slopes.
    float receiver=q.z-PassInfo.x;
    // Receiver plane in normalized depth per uv, built as directionalShadow does; same 2-degree world slope limit as shadowMap.
    float2 gradient=float2(dot(n,float3(ActorMatrix[0].x,ActorMatrix[1].x,ActorMatrix[2].x)),
                           -dot(n,float3(ActorMatrix[0].y,ActorMatrix[1].y,ActorMatrix[2].y)))*ActorInfo.z*ActorInfo.z;
    gradient*=confidence*2*ShadowRange.x/(facing<0?-max(abs(facing),.15):max(abs(facing),.15));
    float limit=57.3*ActorInfo.z*ShadowRange.x;gradient=clamp(gradient,-limit,limit);
    float texelWorld=2*ActorInfo.y*ActorInfo.z;
    float2 pixel=mapUV/ActorInfo.y-.5;
    // Blocker search, 41 taps: the 5x5 texels around the receiver and a 4x4 grid out to the widest penumbra (~3 texels apart,
    // so thin wing tips are found too). No blocker: lit (most pixels stop here). The 5x5 reaches 2.5 texels from the receiver
    // (|nearest-pixel|<=.5, plus 2), past the narrowest penumbra's tent (ActorPenumbraMin, ~2.13 texels): every texel the
    // narrowest filter weighs is searched, so the early out never cuts a contact edge (a 3x3 reached only 1.5 texels and left
    // a hard lit step 1.5..2.13 texels out that moved with the animation).
    float stride=ActorPenumbraMax/texelWorld*(2/3.);
    // Tile early out (exact: the same result as a search that finds no blocker). Every tap below lies within searchReach texels
    // of pixel per axis (the core 2.5, the grid 1.5 strides plus the rounding .5; ~4.8 < 8), so the at most 2x2 tiles covering
    // pixel+-searchReach hold all of them, and no tap can pass the blocker test when their minimum is at or beyond the
    // receiver plus the largest plane offset over that reach. Lit is the common case: four taps instead of 41.
    float searchReach=soft?max(2.5,1.5*stride+.5):ActorPenumbraMin/texelWorld;
    float2 tileLow=(floor((pixel-searchReach)/ActorTileTexels)+.5)*(ActorTileTexels*ActorInfo.y),tileHigh=(floor((pixel+searchReach)/ActorTileTexels)+.5)*(ActorTileTexels*ActorInfo.y);
    float tileNearest=min(min(tex2Dlod(ActorTileMap,float4(tileLow,0,0)).r,tex2Dlod(ActorTileMap,float4(tileHigh.x,tileLow.y,0,0)).r),
                          min(tex2Dlod(ActorTileMap,float4(tileLow.x,tileHigh.y,0,0)).r,tex2Dlod(ActorTileMap,float4(tileHigh,0,0)).r));
    if(tileNearest>=receiver+(abs(gradient.x)+abs(gradient.y))*searchReach*ActorInfo.y+1e-5)return 1;
    float reach=ActorPenumbraMin/texelWorld;
    if(soft&&shadowParameters().y>0){
    float2 nearest=floor(pixel+.5);
    float blockers=0,count=0;
    [loop]for(int grid=0;grid<2;++grid)
    [loop]for(int row=0;row<5-grid;++row)
    [loop]for(int col=0;col<5-grid;++col){
        float2 tap=grid==0?nearest+float2(col,row)-2:floor(pixel+(float2(col,row)-1.5)*stride+.5);
        float2 tapUV=(tap+.5)*ActorInfo.y;
        float z=tex2Dlod(ActorMap,float4(tapUV,0,0)).r;
        if(z<receiver+dot(gradient,tapUV-mapUV)){blockers+=receiver+dot(gradient,tapUV-mapUV)-z;++count;}
    }
    if(count<1)return 1;
    reach=clamp(ActorPenumbraScale*shadowParameters().y*(blockers/count)/ShadowRange.x,ActorPenumbraMin,ActorPenumbraMax)/texelWorld;
    }
    // Continuous tent of half width reach texels (shadowMap's tent at a variable reach) on every texel, weight
    // max(reach-|tap-pixel|,0) per axis; at most 9x9 taps at the widest penumbra. The window starts at the first texel inside
    // the support; a tap enters or leaves it only at zero weight, so the result is continuous in the receiver position and
    // in the reach (no texel-crossing pops while a caster animates; a coarser step for wide reaches made it jump).
    float size=floor(2*reach)+1;
    float2 first=floor(pixel-reach)+1;
    float visibility=0,total=0;
    int taps=0;
    [loop]for(int tapRow=0;tapRow<9&&tapRow<size;++tapRow)
    [loop]for(int tapCol=0;tapCol<size&&taps<81;++tapCol,++taps){
        float2 tap=first+float2(tapCol,tapRow);
        float2 tapUV=(tap+.5)*ActorInfo.y;
        float2 weight=max(reach-abs(tap-pixel),0);
        float w=weight.x*weight.y;
        visibility+=(receiver+dot(gradient,tapUV-mapUV)<=tex2Dlod(ActorMap,float4(tapUV,0,0)).r?1:0)*w;total+=w;
    }
    // Fade to lit toward the border: only casters inside the inset (.8) leave the near cascade, so their shadows end inside it.
    float v=lerp(1,visibility/max(total,.0001),saturate((1-border)/.12));
    return float4(v,v,v,1);
}
float4 ActorShadow(float2 uv:TEXCOORD0,uniform row_major float4x4 ActorMatrix:register(c68),uniform float4 ActorInfo:register(c72)):COLOR0 {return actorShadowValue(uv,ActorMatrix,ActorInfo,true);}
float4 ActorShadowUniform(float2 uv:TEXCOORD0,uniform row_major float4x4 ActorMatrix:register(c68),uniform float4 ActorInfo:register(c72)):COLOR0 {return actorShadowValue(uv,ActorMatrix,ActorInfo,false);}
// Resolve both caster domains directly into the FP16 directional visibility.
// Actor depth/tile tests and world PCSS retain their own projections and filters.
// A fully occluded actor receiver cannot be made brighter by the world maps.
float4 UnifiedDirectionalShadow(float2 uv:TEXCOORD0,uniform row_major float4x4 ActorMatrix:register(c68),uniform float4 ActorInfo:register(c72)):COLOR0 {
    float actor=actorShadowValue(uv,ActorMatrix,ActorInfo,true).r;
    if(actor<=0)return float4(0,0,0,1);
    float visibility=min(actor,DirectionalShadow(uv).r);
    return float4(visibility,visibility,visibility,1);
}
