#include "world_effects.hlsl"

// Experimental common depth cascade. Not part of the production shader pack.
// Positive depths identify world casters, negative depths identify actors.
// abs(encoded) is the unchanged R32F depth; classification costs no mantissa bits.
float4 CommonUnion(float2 uv:TEXCOORD0):COLOR0 {
    float world=tex2Dlod(ShadowNear,float4(uv,0,0)).r;
    float actor=tex2Dlod(ActorMap,float4(uv,0,0)).r;
    return (actor<world?-max(actor,1e-30):world).xxxx;
}

// Each RGBA32F texel describes 4x4 fine depths. The two base-eight integers
// store four row counts and four row first moments, exactly (at most 3510).
float4 CommonBlocks(float2 uv:TEXCOORD0):COLOR0 {
    float2 start=floor(uv*256)*4;
    float low=1,high=0,counts=0,moments=0,scale=1;
    [unroll]for(int y=0;y<4;++y){
        [unroll]for(int x=0;x<4;++x){
            float z=abs(tex2Dlod(ActorMap,float4((start+float2(x,y)+.5)/1024,0,0)).r);
            low=min(low,z);
            if(z<1){high=max(high,z);counts+=scale;moments+=x*scale;}
        }
        scale*=8;
    }
    return float4(low,high,counts,moments);
}

sampler2D CommonBlockInput:register(s13);
float4 CommonTiles(float2 uv:TEXCOORD0):COLOR0 {
    float2 start=floor(uv*64)*4-4;
    float low=1,high=0;
    [loop]for(int y=0;y<12;++y)[loop]for(int x=0;x<12;++x){
        float4 b=tex2Dlod(CommonBlockInput,float4((start+float2(x,y)+.5)/256,0,0));
        low=min(low,b.x);high=max(high,b.z==2340?b.y:1);
    }
    return float4(low,high,0,0);
}

row_major float4x4 FineMatrix:register(c68);
float4 FineInfo:register(c72); // enabled, inverse size, radius, max penumbra in world units
float4 FineDirection:register(c74);
sampler2D FineBlocks:register(s13); // exact min/max of 4x4 fine texels

float2 commonMinMax(float2 pixel,float reach,float texel) {
    float2 lo=(floor((pixel-reach)/16)+.5)*(16*texel);
    float2 hi=(floor((pixel+reach)/16)+.5)*(16*texel);
    float2 a=tex2Dlod(ActorTileMap,float4(lo,0,0)).rg;
    float2 b=tex2Dlod(ActorTileMap,float4(hi.x,lo.y,0,0)).rg;
    float2 c=tex2Dlod(ActorTileMap,float4(lo.x,hi.y,0,0)).rg;
    float2 d=tex2Dlod(ActorTileMap,float4(hi,0,0)).rg;
    return float2(min(min(a.x,b.x),min(c.x,d.x)),max(max(a.y,b.y),max(c.y,d.y)));
}

float finePCF(float2 pixel,float2 mapUV,float receiver,float2 gradient,float reach,bool blocks) {
    float visibility=0,total=0;
    float2 first=floor(pixel-reach)+1;
    float extent=floor(2*reach)+1;
    if(!blocks||reach<=4.25) {
        [loop]for(int y=0;y<35&&y<extent;++y){
            float row=first.y+y;
            float wy=max(reach-abs(row-pixel.y),0);
            [loop]for(int x=0;x<35&&x<extent;++x){
                float2 tap=float2(first.x+x,row),uv=(tap+.5)*FineInfo.y;
                float w=max(reach-abs(tap.x-pixel.x),0)*wy;
                float expected=receiver+dot(gradient,uv-mapUV);
                visibility+=(expected<=abs(tex2Dlod(ActorMap,float4(uv,0,0)).r)?1:0)*w;total+=w;
            }
        }
    } else {
        float2 begin=floor(first/4),end=floor((first+extent-1)/4);
        [loop]for(int by=0;by<10&&by<=end.y-begin.y;++by)
        [loop]for(int bx=0;bx<10&&bx<=end.x-begin.x;++bx){
            float2 tile=begin+float2(bx,by),start=tile*4;
            float4 wx=max(reach-abs(start.x+float4(0,1,2,3)-pixel.x),0);
            float4 wy=max(reach-abs(start.y+float4(0,1,2,3)-pixel.y),0);
            float weight=dot(wx,1)*dot(wy,1);
            float4 range=tex2Dlod(FineBlocks,float4((tile+.5)*4*FineInfo.y,0,0));
            float expected=receiver+dot(gradient,(start+2)*FineInfo.y-mapUV);
            float pad=(abs(gradient.x)+abs(gradient.y))*1.5*FineInfo.y+1e-6;
            if(range.x>=expected+pad){visibility+=weight;total+=weight;}
            else if(range.y<expected-pad&&expected+pad<=1&&range.z==2340)total+=weight;
            else if(all(tile>=0)&&all(tile<256)&&range.y<expected-pad&&expected+pad<=1&&
                    (start.x+3<=pixel.x||start.x>=pixel.x)&&start.x>=pixel.x-reach&&start.x+3<=pixel.x+reach){
                float4 rowCount=floor(frac(range.z/float4(8,64,512,4096))*8+.0001);
                float4 rowMoment=floor(frac(range.w/float4(8,64,512,4096))*8+.0001);
                float sign=start.x>=pixel.x?-1:1;
                float4 covered=(reach-abs(start.x-pixel.x))*rowCount+sign*rowMoment;
                visibility+=weight-dot(covered,wy);total+=weight;
            }
            else {
                [loop]for(int y=0;y<4;++y)[loop]for(int x=0;x<4;++x){
                    float2 tap=start+float2(x,y),uv=(tap+.5)*FineInfo.y;
                    float2 w=max(reach-abs(tap-pixel),0);float product=w.x*w.y;
                    float z=receiver+dot(gradient,uv-mapUV);
                    visibility+=(z<=abs(tex2Dlod(ActorMap,float4(uv,0,0)).r)?1:0)*product;total+=product;
                }
            }
        }
    }
    return visibility/max(total,.0001);
}

float2 commonFine(float2 uv,bool blocks) {
    float2 screen=uv;uv=depthUV(uv);float d=normalizedDepth(uv);
    if(FineInfo.x<.5||d>=.99999||waterDistance(uv,d)>0)return float2(1,0);
    float3 p=worldPosition(uv,d),coarse=affinePoint(p,FineMatrix);
    if(max(abs(coarse.x),abs(coarse.y))>=1.01)return float2(1,0);
    float4 n=tex2Dlod(NormalBuffer,float4(screen,0,0));float facing=dot(n.xyz,FineDirection.xyz);
    float lift=.04+(1-saturate(abs(facing)))*2*FineInfo.y*FineInfo.z;
    float3 q=affinePoint(p+n.xyz*(lift*n.w),FineMatrix);
    float border=max(abs(q.x),abs(q.y)),coverage=saturate((1-border)/.12);
    if(border>=1||q.z<=0||q.z>=1)return float2(1,0);
    float2 mapUV=float2(q.x*.5+.5,.5-q.y*.5)+.5*FineInfo.y;
    float receiver=q.z-PassInfo.x;
    float2 gradient=float2(dot(n.xyz,float3(FineMatrix[0].x,FineMatrix[1].x,FineMatrix[2].x)),
                         -dot(n.xyz,float3(FineMatrix[0].y,FineMatrix[1].y,FineMatrix[2].y)))*FineInfo.z*FineInfo.z;
    gradient*=n.w*2*ShadowRange.x/(facing<0?-max(abs(facing),.15):max(abs(facing),.15));
    float limit=57.3*FineInfo.z*ShadowRange.x;gradient=clamp(gradient,-limit,limit);
    float texelWorld=2*FineInfo.y*FineInfo.z,maxWidth=FineInfo.w;
    float2 pixel=mapUV/FineInfo.y-.5;
    float stride=maxWidth/texelWorld*(2/3.);
    float searchReach=max(2.5,1.5*stride+.5);
    float2 range=commonMinMax(pixel,searchReach,FineInfo.y);
    float pad=(abs(gradient.x)+abs(gradient.y))*searchReach*FineInfo.y+1e-5;
    if(blocks&&range.x>=receiver+pad)return float2(1,coverage);
    if(blocks&&range.y<receiver-pad)return float2(0,coverage);
    float reach=ActorPenumbraMin/texelWorld;
    if(shadowParameters().y>0){
        float2 nearest=floor(pixel+.5);float gaps=0,count=0,worldCount=0;
        [loop]for(int grid=0;grid<3;++grid)
        [loop]for(int row=0;row<(grid==0?5:4);++row)
        [loop]for(int col=0;col<(grid==0?5:4);++col){
            float step=grid==1?ActorPenumbraMax/texelWorld*(2/3.):stride;
            float2 tap=grid==0?nearest+float2(col,row)-2:floor(pixel+(float2(col,row)-1.5)*step+.5);
            float2 uv=(tap+.5)*FineInfo.y;
            float encoded=tex2Dlod(ActorMap,float4(uv,0,0)).r;
            float2 z=float2(abs(encoded),encoded>=0?1:0);
            float gap=receiver+dot(gradient,uv-mapUV)-z.r;
            if(gap>0&&(grid==0||(grid==1?z.g<.5:z.g>=.5))){gaps+=gap;++count;worldCount+=z.g>=.5?1:0;}
        }
        if(count<1)return float2(1,coverage);
        float cap=lerp(ActorPenumbraMax,maxWidth,worldCount/count);
        reach=clamp(.04*shadowParameters().y*gaps/count/ShadowRange.x,ActorPenumbraMin,cap)/texelWorld;
    }
    return float2(finePCF(pixel,mapUV,receiver,gradient,reach,blocks),coverage);
}

float4 CommonDirectional(float2 uv:TEXCOORD0):COLOR0 {
    float2 fine=commonFine(uv,true);
    float v=fine.x;
    if(fine.y<1)v=lerp(DirectionalShadow(uv).r,v,fine.y);
    return float4(v,v,v,1);
}
float4 CommonReference(float2 uv:TEXCOORD0):COLOR0 {
    float2 fine=commonFine(uv,false);
    float v=fine.x;
    if(fine.y<1)v=lerp(DirectionalShadow(uv).r,v,fine.y);
    return float4(v,v,v,1);
}

// Independent CPU-oracle tests supply pixel.xy, support radius and receiver
// depth. Isolate the PCF integrator from projection and blocker-search rounding.
float4 CommonKernel(float2 uv:TEXCOORD0,uniform float4 gradient:register(c81)):COLOR0 {
    float4 q=tex2Dlod(Scene,float4(uv,0,0));
    return finePCF(q.xy,(q.xy+.5)*FineInfo.y,q.w,gradient.xy,q.z,true).xxxx;
}
float4 CommonKernelReference(float2 uv:TEXCOORD0,uniform float4 gradient:register(c81)):COLOR0 {
    float4 q=tex2Dlod(Scene,float4(uv,0,0));
    return finePCF(q.xy,(q.xy+.5)*FineInfo.y,q.w,gradient.xy,q.z,false).xxxx;
}

ShadowReplayOutput CommonReplayPS(float2 uv:TEXCOORD0,float4 lightClip:TEXCOORD7,float4 diffuse:COLOR0) {
    ShadowReplayOutput output=ShadowReplayPS(uv,lightClip,diffuse);
    output.native.rgb=-max(output.native.rgb,1e-30);
    output.depth=-max(output.depth,1e-30);
    return output;
}

struct CommonSeedOutput {float4 color:COLOR0;float depth:DEPTH;};
CommonSeedOutput CommonSeedDepth(float2 uv:TEXCOORD0) {
    CommonSeedOutput output;output.depth=tex2Dlod(Scene,float4(uv,0,0)).r;
    output.color=output.depth.xxxx;return output;
}
