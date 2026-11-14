// D3D9 frustum volume: 64 radial, cubically spaced slices in an 8x8 atlas.
#if VOLUME_TRANSITION
#include "shadow_transition.hlsl"
#else
#include "world_effects.hlsl"
#endif
float4 VolumeGrid : register(c154); // cell width/height, reciprocal atlas width/height
float4 VolumePass : register(c155); // range, prefix stride (0 = coefficients), history weight
float4 VolumePrevious : register(c156); // previous projection xy, view sign, range
sampler2D VolumeInput : register(s0);
sampler2D VolumeHistory : register(s2);

float3 volumeCell(float2 uv) {
    float2 pixel=floor(uv/VolumeGrid.zw);
    float2 tile=floor(pixel/VolumeGrid.xy);
    return float3((pixel-tile*VolumeGrid.xy+.5)/VolumeGrid.xy,tile.x+8*tile.y);
}
float2 volumeUV(float2 uv,float slice) {
    uv=clamp(uv,.5/VolumeGrid.xy,1-.5/VolumeGrid.xy);
    return (float2(slice-8*floor(slice/8),floor(slice/8))+uv)/8;
}
float4 volumeAt(sampler2D image,float2 uv,float slice) {
    return tex2Dlod(image,float4(volumeUV(uv,clamp(slice,0,63)),0,0));
}
float volumeDistance(float slice) {return VolumePass.x*slice*slice*slice*(1.0/262144);}
float3 volumeRay(float2 uv) {
    float3 v=float3((uv*float2(2,-2)+float2(-1,1))/Projection.xy,Projection.z);
    return normalize(v.x*InverseView[0].xyz+v.y*InverseView[1].xyz+v.z*InverseView[2].xyz);
}
float4 volumeInjectPoint(float3 ray,float a,float b,float t,bool actor,bool noisy) {
    float3 p=Camera.xyz+ray*t;float coverage;
    float4 field=regionalFogAt((p.xy-RegionalFogInfo.xy)*RegionalFogInfo.z+.5/64,coverage);
    float inside=all(p.xy>=RegionalFogInfo.xy)&&all(p.xy<=RegionalFogInfo.xy+63.0/(64*max(RegionalFogInfo.z,1e-8)));
    // Match foreverFog: a top-surface field cannot classify air under roofs.
    // Authored height profiles place the medium; shadows only light it.
    float valid=inside?coverage:1;
    float ground=coverage>0?field.x:Forever[0].w;
    float3 height=p.z-ground*Forever[10].xyz;
    float3 shape=exp(min(0,(Forever[2].xyz-height)*Forever[3].xyz)+min(0,(height-Forever[4].xyz)*Forever[5].xyz));
    float3 curve=1+Forever[7].xyz*pow(max(saturate((t-Forever[1].xyz)*.001),1e-8),Forever[8].xyz);
    float3 density=Forever[0].xyz*shape*curve*step(Forever[1].xyz,t)*valid;
    float3 noise=1;
    [branch]if(noisy&&dot(density,1)>0)noise=foreverNoise(p);
    density*=noise;
    float visible=foreverVisibility(ray,a,b,t,actor,true);
    float3 g=Forever[6].xyz;
    float3 phase=(1.0/(4*3.14159265))*(1-g*g)*pow(rsqrt(max(1+g*g-2*g*dot(ray,SunDirection.xyz),1e-6)),3);
    float3 lit=lerp(1,visible,Forever[9].xyz)*phase*Forever[11].xyz*Forever[11].w;
    float3 ambient=SourcePolicy.x*lerp(1,visible,Forever[9].xyz),shade=SourcePolicy.x*(1-lerp(1,visible,Forever[9].xyz));
    float3 s0=Forever[12].rgb*lit.x+lerp(Forever[35].rgb,Forever[15].rgb,noise.x)*ambient.x+Forever[18].rgb*shade.x;
    float3 s1=Forever[13].rgb*lit.y+lerp(Forever[36].rgb,Forever[16].rgb,noise.y)*ambient.y+Forever[19].rgb*shade.y;
    float3 s2=Forever[14].rgb*lit.z+lerp(Forever[37].rgb,Forever[17].rgb,noise.z)*ambient.z+Forever[20].rgb*shade.z;
    return float4(s0*density.x+s1*density.y+s2*density.z,dot(density,1));
}
float4 volumeInject(float2 uv,bool actor,bool noisy) {
    float3 cell=volumeCell(uv),ray=volumeRay(cell.xy);
    float a=volumeDistance(cell.z),b=volumeDistance(cell.z+1);
    // Two-point Gauss quadrature resolves noise inside distant, wide slices
    // without doubling atlas memory. The host adds the two raster passes;
    // an in-shader loop exceeds SM3's 32 temporaries in the actor variant.
    float t=noisy?lerp(a,b,VolumePass.w):volumeDistance(cell.z+.5);
    return volumeInjectPoint(ray,a,b,t,actor,noisy)*(noisy?.5:1);
}
float4 VolumeInject(float2 uv:TEXCOORD0):COLOR0 {return volumeInject(uv,false,true);}
float4 VolumeInjectQuiet(float2 uv:TEXCOORD0):COLOR0 {return volumeInject(uv,false,false);}
float4 VolumeInjectActor(float2 uv:TEXCOORD0):COLOR0 {return volumeInject(uv,true,true);}
float4 VolumeInjectActorQuiet(float2 uv:TEXCOORD0):COLOR0 {return volumeInject(uv,true,false);}

float4 VolumeTemporal(float2 uv:TEXCOORD0):COLOR0 {
    float3 cell=volumeCell(uv);float4 current=volumeAt(VolumeInput,cell.xy,cell.z);
    if(VolumePass.z<=0)return current;
    float t=volumeDistance(cell.z+.5);
    float3 p=affinePoint(Camera.xyz+volumeRay(cell.xy)*t,PreviousView);
    float z=p.z*VolumePrevious.z;
    float2 oldUV=float2(p.x,-p.y)*VolumePrevious.xy/(2*max(z,1e-8))+.5;
    float oldSlice=pow(max(length(p)/VolumePrevious.w,1e-12),1.0/3)*64-.5;
    if(z<=0||any(oldUV<0)||any(oldUV>1)||oldSlice<-.001||oldSlice>63.001)return current;
    oldSlice=clamp(oldSlice,0,63);
    float4 lo=current,hi=current;
    [unroll]for(int axis=0;axis<3;++axis){
        float3 offset=axis==0?float3(1/VolumeGrid.x,0,0):axis==1?float3(0,1/VolumeGrid.y,0):float3(0,0,1);
        float4 a=volumeAt(VolumeInput,cell.xy+offset.xy,cell.z+offset.z),b=volumeAt(VolumeInput,cell.xy-offset.xy,cell.z-offset.z);
        lo=min(lo,min(a,b));hi=max(hi,max(a,b));
    }
    float base=floor(oldSlice);
    float4 history=lerp(volumeAt(VolumeHistory,oldUV,base),volumeAt(VolumeHistory,oldUV,base+1),oldSlice-base);
    return lerp(current,clamp(history,lo,hi),VolumePass.z);
}
float4 volumeSegment(float4 coefficients,float distance) {
    float T=exp(-coefficients.a*distance);
    // Store opacity: FP16 render targets can truncate T just below 1 by an
    // entire half-float ULP per slice, inventing fog in a nearly clear medium.
    return float4(coefficients.rgb*((1-T)/max(coefficients.a,1e-8)),1-T);
}
float4 VolumePrefix(float2 uv:TEXCOORD0):COLOR0 {
    float3 cell=volumeCell(uv);float4 far=volumeAt(VolumeInput,cell.xy,cell.z);
    if(VolumePass.y==0)return volumeSegment(far,volumeDistance(cell.z+1)-volumeDistance(cell.z));
    float4 near=cell.z>=VolumePass.y?volumeAt(VolumeInput,cell.xy,cell.z-VolumePass.y):0;
    return float4(near.rgb+(1-near.a)*far.rgb,near.a+(1-near.a)*far.a);
}
float4 VolumeResolve(float2 uv:TEXCOORD0):COLOR0 {
    uv=depthUV(uv);float depth=normalizedDepth(uv);
    float distance=min(length(viewPositionDistance(uv,fogReceiverDistance(uv,depth))),VolumePass.x);
    float s=min(floor(pow(distance/VolumePass.x,1.0/3)*64),63);
    float a=volumeDistance(s),b=volumeDistance(s+1);
    float2 rayUV=uv-.5*ImageClip.xy;
    float4 near=s>0?volumeAt(VolumeInput,rayUV,s-1):0;
    float4 far=volumeAt(VolumeInput,rayUV,s);
    // Fractional Beer-Lambert segment, not a linear opacity interpolation.
    float fraction=saturate((distance-a)/max(b-a,1e-8));
    float ratio=(1-far.a)/max(1-near.a,1e-8),T=pow(max(ratio,1e-8),fraction);
    float weight=abs(1-ratio)>1e-5?(1-T)/(1-ratio):fraction;
    return float4(near.rgb+(far.rgb-near.rgb)*weight,(1-near.a)*T);
}
