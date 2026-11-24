// Original material coverage and depth are shared by the normal and occlusion targets.
sampler2D Diffuse:register(s0);
sampler2D LayerMask:register(s1);
sampler2D GeneratedNormal:register(s2);
sampler2D GeneratedNormal1:register(s3);
float4 NativeLayerMask:register(c0);
float4 BumpCapture:register(c20); // w: WMO opaque coverage offset; M2 native-alpha selector
float4 CaptureProjection:register(c21);
float4 ParallaxControl:register(c22);
float4 OcclusionControl:register(c23); // shadow strength, shadow steps, cavity strength, near range (0 disables)
float4 OcclusionLight:register(c24); // view direction, 1 sun / 2 moon / 0 inactive
float4 CaptureToWorld[3]:register(c26); // current camera's view vectors to world
// Independent of material detail: native coverage and the skinned view normal.
float4 ModelNormalCapture(float3 normal:TEXCOORD5,float4 p:TEXCOORD7,float4 nativeColor:COLOR0):COLOR0 {
    clip(BumpCapture.y-p.w);
    clip(lerp(1,nativeColor.a,BumpCapture.w)-BumpCapture.z);
    return float4(normalize(normal),p.w);
}
float4 ModelNormalOpaque(float3 normal:TEXCOORD5,float4 p:TEXCOORD7):COLOR0 {
    clip(BumpCapture.y-p.w);
    return float4(normalize(normal),p.w);
}
// Far bound for WorldNormals' FP16 distance acceptance, in private depth only.
float4 ModelSeedView:register(c0); // near, far, resolved min Z, inverse range
float4 ModelSeedProjection:register(c1); // A/C, B, viewport min Z, viewport range
struct ModelSeedOutput {float4 color:COLOR0;float depth:DEPTH;};
ModelSeedOutput ModelNormalDepthSeed(float2 uv:TEXCOORD0) {
    float d=saturate((tex2D(Diffuse,uv).r-ModelSeedView.z)*ModelSeedView.w);
    float z=ModelSeedView.x*ModelSeedView.y/max(ModelSeedView.y-d*(ModelSeedView.y-ModelSeedView.x),.00001);
    float guard=(z+max(.006,z*.0015))*(1+1.0/512)+.0001;
    float hardware=ModelSeedProjection.z+ModelSeedProjection.w*(ModelSeedProjection.x+ModelSeedProjection.y/guard);
    ModelSeedOutput o;o.color=0;o.depth=d>=.99999?1:saturate(hardware+64.0/16777215);return o;
}
#ifdef NORTHLIGHT_SHARED_POM
float4 SharedPomControl:register(c25);
#define POM_ID_INPUT ,float primitive:TEXCOORD6
#define POM_ID_PARAMETER ,float primitive
#define POM_ID_ARGUMENT ,primitive
#else
#define POM_ID_INPUT
#define POM_ID_PARAMETER
#define POM_ID_ARGUMENT
#endif
#include "auto_parallax.hlsl"
struct MaterialValue {float3 normal,reference,occlusion;float2 uv,uv1;};
struct CaptureResult {float4 normal:COLOR0;float4 occlusion:COLOR1;
#ifdef NORTHLIGHT_SHARED_POM
    float4 uv:COLOR2;float4 uv1:COLOR3;
#endif
};
float2 octEncode(float3 n){n/=max(dot(abs(n),1),1e-6);return (n.z>=0?n.xy:(1-abs(n.yx))*float2(n.x>=0?1:-1,n.y>=0?1:-1))*.5+.5;}
MaterialValue materialValue(sampler2D map,float2 uv,float4 p,float3 smoothNormal,float4 parallaxControl,bool parallax) {
    MaterialFrame f=materialFrame(uv,p,CaptureProjection);
    MaterialValue v;v.uv=parallax?parallaxUVFrame(map,uv,p.w,parallaxControl,f):uv;v.uv1=0;
    float4 data=tex2Dgrad(map,v.uv,f.tx,f.ty);
    float2 xy=(data.rg*255-128)/127;
    float3 local=float3(xy,sqrt(saturate(1-dot(xy,xy))));
    float3 n=f.normal,t=f.tangent,b=f.bitangent;
    if(dot(smoothNormal,smoothNormal)>1e-12){
        n=normalize(smoothNormal);t-=n*dot(t,n);b-=n*dot(b,n);
        t*=rsqrt(max(dot(t,t),1e-12));b*=rsqrt(max(dot(b,b),1e-12));
    }
    v.reference=n;v.normal=normalize(t*local.x+b*local.y+n*local.z);
    v.occlusion=1;
    if(!parallax)v.occlusion.b=p.w<OcclusionControl.w?lerp(1,data.a,OcclusionControl.z):1;
    else [branch]if(p.w<OcclusionControl.w){
        v.occlusion.b=lerp(1,data.a,OcclusionControl.z);
        if(OcclusionControl.x>0){
            float shade=lerp(1,parallaxShadow(map,v.uv,data.b,p.w,f,parallaxControl,OcclusionLight,OcclusionControl.y),OcclusionControl.x);
            v.occlusion.rg=OcclusionLight.w>1.5?float2(1,shade):float2(shade,1);
        }
    }
    return v;
}
MaterialValue materialValue(sampler2D map,float2 uv,float4 p,float3 smoothNormal){return materialValue(map,uv,p,smoothNormal,ParallaxControl,true);}
MaterialValue materialValue(sampler2D map,float2 uv,float4 p){return materialValue(map,uv,p,0);}
MaterialValue normalMaterialValue(sampler2D map,float2 uv,float4 p,float3 smoothNormal){return materialValue(map,uv,p,smoothNormal,float4(0,ParallaxControl.yzw),true);}
MaterialValue normalMaterialValue(sampler2D map,float2 uv,float4 p){return normalMaterialValue(map,uv,p,0);}
CaptureResult captureRelativeValue(MaterialValue v,float4 p,float alpha POM_ID_PARAMETER,bool alphaChecked=false) {
    if(!alphaChecked)clip(alpha-BumpCapture.z);
    // Store the perturbation relative to the capture's smooth reference.
    // A neutral map is (0,0,1), even when scene-depth normals differ from it.
    // Keep the reference away from the codec's -Z pole. The octahedron's
    // lower hemisphere carries this choice to the scene-normal rebase.
    // Roundoff around a vertical wall must not switch the chosen hemisphere.
    float hemisphere=v.reference.z<-1e-4?-1:1;
    v.normal*=hemisphere;v.reference*=hemisphere;
    float3 delta=v.normal-v.reference;
    float along=mad(dot(v.reference.xy,delta.xy),rcp(max(1+v.reference.z,1e-4)),delta.z);
    float3 detail=float3(mad(-v.reference.xy,along,delta.xy),max(dot(v.reference,v.normal),0));
    detail.z*=hemisphere;float2 encoded=octEncode(detail);
    CaptureResult o;o.normal=float4(encoded,p.w,saturate(alpha));o.occlusion=float4(v.occlusion,saturate(alpha));
#ifdef NORTHLIGHT_SHARED_POM
    o.uv=float4(v.uv,SharedPomControl.z+floor(primitive+.5),1);o.uv1=float4(v.uv1,0,1);
#endif
    return o;
}
CaptureResult captureValue(MaterialValue v,float4 p,float alpha POM_ID_PARAMETER,bool alphaChecked=false,bool terrain=false,bool foliage=false){
    // Rebase in a fixed frame. Rebasing between two different surface normals
    // in view space changes the resulting world normal when the camera turns.
    v.normal=v.normal.x*CaptureToWorld[0].xyz+v.normal.y*CaptureToWorld[1].xyz+v.normal.z*CaptureToWorld[2].xyz;
    v.reference=v.reference.x*CaptureToWorld[0].xyz+v.reference.y*CaptureToWorld[1].xyz+v.reference.z*CaptureToWorld[2].xyz;
    CaptureResult o=captureRelativeValue(v,p,alpha POM_ID_ARGUMENT,alphaChecked);
    // Signed depth identifies world-frame detail without another render target.
    o.normal.b=-p.w;
    // FP16 sign marks terrain receivers while preserving the octahedral value.
    // Sign pairs preserve octahedral magnitudes: terrain R, structures G, foliage RG.
    if(terrain||foliage)o.normal.r=-o.normal.r;
    if(BumpCapture.x<0||foliage)o.normal.g=-o.normal.g;
    return o;
}
CaptureResult BumpOpaque(float2 uv:TEXCOORD0,float4 p:TEXCOORD7 POM_ID_INPUT){clip(BumpCapture.y-p.w);return captureValue(materialValue(GeneratedNormal,uv,p),p,1 POM_ID_ARGUMENT);}
CaptureResult BumpLayer(float2 uv:TEXCOORD0,float2 maskUV:TEXCOORD1,float4 p:TEXCOORD7 POM_ID_INPUT){clip(BumpCapture.y-p.w);float alpha=saturate(dot(tex2D(LayerMask,maskUV),NativeLayerMask));clip(alpha-BumpCapture.z);return captureValue(materialValue(GeneratedNormal,uv,p),p,alpha POM_ID_ARGUMENT,true);}
CaptureResult BumpTerrainOpaque(float2 uv:TEXCOORD0,float3 normal:TEXCOORD5,float4 p:TEXCOORD7 POM_ID_INPUT){clip(BumpCapture.y-p.w);return captureValue(materialValue(GeneratedNormal,uv,p,normal),p,1 POM_ID_ARGUMENT,false,true);}
CaptureResult BumpTerrainLayer(float2 uv:TEXCOORD0,float2 maskUV:TEXCOORD1,float3 normal:TEXCOORD5,float4 p:TEXCOORD7 POM_ID_INPUT){clip(BumpCapture.y-p.w);float alpha=saturate(dot(tex2D(LayerMask,maskUV),NativeLayerMask));clip(alpha-BumpCapture.z);return captureValue(materialValue(GeneratedNormal,uv,p,normal),p,alpha POM_ID_ARGUMENT,true,true);}
CaptureResult BumpWall(float2 uv:TEXCOORD0,float4 color:COLOR0,float4 p:TEXCOORD7 POM_ID_INPUT){clip(BumpCapture.y-p.w);
    MaterialValue v=materialValue(GeneratedNormal,uv,p);return captureValue(v,p,tex2Dgrad(Diffuse,v.uv,ddx(uv),ddy(uv)).a*color.a+BumpCapture.w POM_ID_ARGUMENT);
}
CaptureResult BumpGrass(float2 uv:TEXCOORD0,float4 fade:COLOR1,float4 p:TEXCOORD7 POM_ID_INPUT){clip(BumpCapture.y-p.w);float alpha=tex2D(Diffuse,uv).a*fade.x;clip(alpha-BumpCapture.z);return captureValue(materialValue(GeneratedNormal,uv,p),p,alpha POM_ID_ARGUMENT,true,false,true);}
CaptureResult BumpWallOpaque(float2 uv:TEXCOORD0,float4 color:COLOR0,float4 p:TEXCOORD7 POM_ID_INPUT){clip(BumpCapture.y-p.w);float alpha=color.a+BumpCapture.w;clip(alpha-BumpCapture.z);return captureValue(materialValue(GeneratedNormal,uv,p),p,alpha POM_ID_ARGUMENT,true);}
CaptureResult BumpComposite(float2 uv:TEXCOORD0,float2 uv1:TEXCOORD1,float4 color:COLOR0,float4 blend:COLOR1,float4 p:TEXCOORD7 POM_ID_INPUT){clip(BumpCapture.y-p.w);
    MaterialValue first=materialValue(GeneratedNormal,uv,p),second=materialValue(GeneratedNormal1,uv1,p);
    // Add the solid-coverage offset before the existing saturation. Folding it
    // into MAD keeps the shared composite within the 512-slot SM3 limit.
    float alpha=mad(mad(tex2Dgrad(Diffuse,first.uv,ddx(uv),ddy(uv)).a-tex2Dgrad(LayerMask,second.uv,ddx(uv1),ddy(uv1)).a,blend.w,tex2Dgrad(LayerMask,second.uv,ddx(uv1),ddy(uv1)).a),color.a,BumpCapture.w);
    first.uv1=second.uv;first.normal=mad(first.normal-second.normal,saturate(blend.w),second.normal);first.occlusion=mad(first.occlusion-second.occlusion,saturate(blend.w),second.occlusion);return captureValue(first,p,alpha POM_ID_ARGUMENT,true);
}

// Native alpha is computed by the original material program in BumpNativePatch.
CaptureResult BumpNative(float2 uv:TEXCOORD0,float4 nativeColor:COLOR0,float4 p:TEXCOORD7 POM_ID_INPUT){clip(BumpCapture.y-p.w);float alpha=lerp(1,nativeColor.a,BumpCapture.w);clip(alpha-BumpCapture.z);return captureValue(materialValue(GeneratedNormal,uv,p),p,alpha POM_ID_ARGUMENT,true);}

// These entries are selected only when the host already disables POM. They
// preserve the same frame, coverage, cavity and relative-normal encoding.
#ifndef NORTHLIGHT_SHARED_POM
CaptureResult BumpNormalNative(float2 uv:TEXCOORD0,float4 nativeColor:COLOR0,float4 p:TEXCOORD7){clip(BumpCapture.y-p.w);float alpha=lerp(1,nativeColor.a,BumpCapture.w);clip(alpha-BumpCapture.z);return captureValue(normalMaterialValue(GeneratedNormal,uv,p),p,alpha,true);}
float4 BumpFusedNative(float2 uv:TEXCOORD0,float4 p:TEXCOORD7):COLOR0 {
    // Native colour and depth still belong to this fragment. Mark only the
    // auxiliary outputs invalid beyond material distance. Keep derivatives
    // outside the per-fragment range branch.
    CaptureResult o=captureValue(normalMaterialValue(GeneratedNormal,uv,p),p,1,true);
    // Opaque coverage is one. Alpha above one carries the cavity UNORM8
    // code in increments of 1/256, exactly representable in FP16.
    o.normal.a=1+(1+floor(saturate(o.occlusion.b)*255+.5))/256;
    return p.w>BumpCapture.y?float4(0,0,0,0):o.normal;
}
float4 BumpClearMS():COLOR0 {return 0;}

// SM3 has no bit casts. Use the sign, exponent and fraction of a normal R32F
// value. Exponents 4..253 also keep the selected sample normal after /8.
float m2Exponent(float v){
    float e=floor(log2(v));
    float unit=exp2(e);
    if(v<unit)return e-1;
    return v>=unit*2?e+1:e;
}
float m2HalfUnits(float v,float exponent){
    // Match the FP16 render target's truncation on the validated DXVK path.
    return floor(v*exp2(10-exponent));
}
float m2HalfDepthCode(float z){
    float e=m2Exponent(z);
    return (e+14)*1024+m2HalfUnits(z,e);
}
float m2HalfOct(float v){
    float e=max(-14,m2Exponent(max(v,1.0/16384)));
    return m2HalfUnits(v,e)*exp2(e-10);
}
float2 m2QuantizedOct(float2 oct){
    float2 q=floor(oct*124+.5);
    float2 centered=q-62;
    float sum=dot(abs(centered),1);
    // The octahedral fold is also a hemisphere tag for the lighting rebase.
    // Preserve the tag the original FP16 target would produce.
    [branch]if(abs(sum-62)<=1){
        float2 halfOct=float2(m2HalfOct(oct.x),m2HalfOct(oct.y));
        bool lower=dot(abs(halfOct-.5),1)>.5;
        float adjust=lower&&sum<=62?63-sum:!lower&&sum>62?62-sum:0;
        bool moveX=adjust>0&&abs(centered.x)==62?false:
                   adjust>0&&abs(centered.y)==62?true:abs(centered.x)>=abs(centered.y);
        if(moveX)q.x+=adjust*(oct.x<.5?-1:1);
        else q.y+=adjust*(oct.y<.5?-1:1);
    }
    return q;
}
float m2PackMaterial(float4 material,float depthBase,float cavityStrength){
    float depth=m2HalfDepthCode(max(-material.b,1.0/16384))-depthBase;
    if(depth<0||depth>16383)return 0;
    float2 q=m2QuantizedOct(material.rg);
    float cavity=floor(saturate((1-material.a)/max(cavityStrength,.001))*15+.5);
    float xHi=floor(q.x/32),xLo=q.x-xHi*32;
    float e=2*q.y+2*frac(xHi*.5)-123;
    float fraction=(xLo*262144+cavity*16384+depth)*(1.0/8388608);
    return (xHi>=2?-1:1)*exp2(e)*(1+fraction);
}
float4 m2UnpackMaterial(float packed,float depthBase,float cavityStrength){
    // Renormalize the carrier before using SM3's approximate log/exp pair.
    float magnitude=abs(packed);
    float shift=magnitude<exp2(-32)?64:magnitude>=exp2(32)?-64:0;
    magnitude*=exp2(shift);
    float scaledExponent=m2Exponent(magnitude);
    float e=scaledExponent-shift;
    // Subtract the exponent before rescaling the fraction to recover the
    // integer payload exactly. No rounding is needed on these fields.
    float bits=(magnitude*exp2(-scaledExponent)-1)*8388608;
    float xLo=floor(bits/262144);
    bits-=xLo*262144;
    float cavity=floor(bits/16384);
    float depth=bits-cavity*16384+depthBase;
    float depthExp=floor(depth/1024);
    float z=(1+frac(depth/1024))*exp2(depthExp-15);
    float y=(e+123)*.5;
    float2 q=float2(xLo+frac(y)*64+(packed<0?64:0),floor(y));
    // An exact 1/2048 grid keeps matched oct coordinates on the upper fold
    // when the old composite reads them back from the FP16 material target.
    float2 oct=floor(q*(2048.0/124)+.5)*(1.0/2048);
    float brightness=1-cavity*(cavityStrength/15);
    return float4(oct,-z,1+(1+floor(saturate(brightness)*255+.5))/256);
}
float4 BumpFusedPackedNative(float2 uv:TEXCOORD0,float4 p:TEXCOORD7):COLOR0 {
    CaptureResult o=captureValue(normalMaterialValue(GeneratedNormal,uv,p),p,1,true);
    float packed=m2PackMaterial(float4(o.normal.rgb,o.occlusion.b),BumpCapture.w,OcclusionControl.z);
    return p.w>BumpCapture.y?0:float4(packed,0,0,0);
}

float4 BumpMergeMSInfo:register(c0); // sample count, viewport min Z, inverse viewport depth range
float4 BumpMergeMSView:register(c1); // near, far
float4 m2MergeMS(float4 fused,float4 legacy,float2 uv){
    float d=saturate((tex2D(GeneratedNormal,uv).r-BumpMergeMSInfo.y)*BumpMergeMSInfo.z);
    float z=BumpMergeMSView.x*BumpMergeMSView.y/max(BumpMergeMSView.y-d*(BumpMergeMSView.y-BumpMergeMSView.x),.00001);
    float tolerance=max(.025,z*.0015);
    float fusedZ=abs(fused.b);
    float cover=saturate(legacy.a);
    float legacyZ=abs(legacy.b)/max(cover,.001);
    bool preferLegacy=cover>.02&&abs(legacyZ-z)<tolerance&&legacyZ<=fusedZ;
    return abs(fusedZ-z)<tolerance&&!preferLegacy?fused:legacy;
}
float4 BumpMergeMS(float2 uv:TEXCOORD0):COLOR0 {
    float4 fused=tex2D(Diffuse,uv)*BumpMergeMSInfo.x;
    float4 legacy=tex2D(LayerMask,uv);
    [branch]if(fused.a<=1)return legacy;
    return m2MergeMS(fused,legacy,uv);
}
float4 BumpMergePackedMS(float2 uv:TEXCOORD0):COLOR0 {
    float packed=tex2D(Diffuse,uv).r*BumpMergeMSInfo.x;
    float4 legacy=tex2D(LayerMask,uv);
    [branch]if(packed==0)return legacy;
    return m2MergeMS(m2UnpackMaterial(packed,BumpMergeMSView.z,BumpMergeMSView.w),legacy,uv);
}
CaptureResult BumpNormalOpaque(float2 uv:TEXCOORD0,float4 p:TEXCOORD7){clip(BumpCapture.y-p.w);return captureValue(normalMaterialValue(GeneratedNormal,uv,p),p,1);}
CaptureResult BumpNormalLayer(float2 uv:TEXCOORD0,float2 maskUV:TEXCOORD1,float4 p:TEXCOORD7){clip(BumpCapture.y-p.w);float alpha=saturate(dot(tex2D(LayerMask,maskUV),NativeLayerMask));clip(alpha-BumpCapture.z);return captureValue(normalMaterialValue(GeneratedNormal,uv,p),p,alpha,true);}
CaptureResult BumpNormalTerrainOpaque(float2 uv:TEXCOORD0,float3 normal:TEXCOORD5,float4 p:TEXCOORD7){clip(BumpCapture.y-p.w);return captureValue(normalMaterialValue(GeneratedNormal,uv,p,normal),p,1,false,true);}
CaptureResult BumpNormalTerrainLayer(float2 uv:TEXCOORD0,float2 maskUV:TEXCOORD1,float3 normal:TEXCOORD5,float4 p:TEXCOORD7){clip(BumpCapture.y-p.w);float alpha=saturate(dot(tex2D(LayerMask,maskUV),NativeLayerMask));clip(alpha-BumpCapture.z);return captureValue(normalMaterialValue(GeneratedNormal,uv,p,normal),p,alpha,true,true);}
CaptureResult BumpNormalGrass(float2 uv:TEXCOORD0,float4 fade:COLOR1,float4 p:TEXCOORD7){clip(BumpCapture.y-p.w);float alpha=tex2D(Diffuse,uv).a*fade.x;clip(alpha-BumpCapture.z);return captureValue(normalMaterialValue(GeneratedNormal,uv,p),p,alpha,true,false,true);}
#endif

// Native canopy epilogue. Reconstruct the leaf plane from interpolated clip
// position so the highlight follows both the actual geometry and the eye.
float4 CanopyProjection:register(c40); // reciprocal projection X/Y, broad crown transmission
float4 CanopyLight:register(c41); // view-space direction, transmission strength
float4 CanopyRadiance:register(c42); // source RGB, reflection strength
float4 CanopyResponse(float4 nativeColor:COLOR0,float4 clipPosition:TEXCOORD7):COLOR0 {
    float3 p=float3(clipPosition.xy*CanopyProjection.xy,clipPosition.w);
    float3 plane=cross(ddx(p),ddy(p));
    float planeLength=dot(plane,plane);
    float3 n=plane*rsqrt(max(planeLength,1e-12));
    float3 v=-normalize(p);
    float facing=dot(n,v);
    n=facing<0?-n:n;
    float incident=dot(n,CanopyLight.xyz);
    float grazing=1-saturate(abs(facing));
    float back=saturate(-dot(v,CanopyLight.xyz))*saturate(-incident);
    float transmission=back*(.20+.80*grazing*grazing)*CanopyLight.w;

    float3 halfway=v+CanopyLight.xyz;
    halfway*=rsqrt(max(dot(halfway,halfway),1e-6));
    float lobe=saturate(dot(n,halfway));
    float lobe2=lobe*lobe;
    float lobe4=lobe2*lobe2;
    float reflection=lobe4*lobe4*saturate(incident*3)*CanopyRadiance.w;
    float fade=saturate((75-clipPosition.w)/45);
    float coverage=saturate(nativeColor.a*1.5);
    float textureWeight=saturate(.25+2*max(nativeColor.r,max(nativeColor.g,nativeColor.b)));
    float response=(max(transmission,CanopyProjection.z)+reflection)*fade*coverage*textureWeight*(planeLength>1e-12);
    return float4(nativeColor.rgb+CanopyRadiance.rgb*response,nativeColor.a);
}
