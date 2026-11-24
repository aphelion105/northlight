// Uses the world pass camera and depth reconstruction functions.
#include "world_view.hlsl"

// Half-resolution smoothed surface normal. Depth-derived normals are constant
// per triangle, while the game painted its lighting with interpolated vertex
// normals; a wide baseline in WORLD units (about 1.5 u each side, clamped in
// pixels) averages neighbouring facets the way vertex normals do, so shadow
// and GI corrections no longer draw triangle edges on low-poly hills. Depth
// discontinuities fall back to the one-pixel neighbour so silhouettes stay crisp.
// A depth test alone is not enough: a character's legs or a post's base sit
// at nearly the ground's depth, and a wide sample landing on them tilted the
// ground normal in a box around every actor (light patches under feet).
// The wide sample must also continue the one-pixel surface direction: on a
// hill the direction bends by half the facet angle at most (accepted), while
// a sample on another object leaves the local plane (rejected, 1-px fallback).
float3 wideNeighbour(float2 uv,float2 dir,float pixels,float3 center,float centerZ,float3 nearPos) { // the valid texel centre range (.5 px, 1-.5 px) is written out: fewer live registers
    float2 q=depthUV(clamp(uv+dir*pixels,.5*ImageClip.xy,1-.5*ImageClip.xy));
    float z=viewDistance(normalizedDepth(q));
    float3 wide=viewPositionDistance(q,z);
    float3 a=nearPos-center,b=wide-center;
    float planar=dot(a,b)*rsqrt(max(dot(a,a)*dot(b,b),1e-12));
    // A shallow secant can end on a WALL while still agreeing with the ground
    // direction. Compare the local tangent at that endpoint as well: crossing
    // the corner must not bend a ground normal toward the wall. This retains
    // smoothing across gentle terrain facets without an offset bright ring.
    float2 inner=clamp(q-dir,.5*ImageClip.xy,1-.5*ImageClip.xy);
    b=wide-viewPosition(inner,normalizedDepth(inner)); // b now the tangent at the wide end (reused: the pass is at its 32-register limit)
    float continuation=dot(a,b)*rsqrt(max(dot(a,a)*dot(b,b),1e-12));
    // r76: .95 (about 18 degrees; was .85, 31): a base up to about .9 u high at the 1.5 u
    // baseline passed .85 and tilted the ground normal about 15 degrees, a bright fin of
    // under-removed native light along the screen axis from every rock, cart and post base.
    return abs(z-centerZ)>max(.35,centerZ*.04)||min(planar,continuation)<.95?nearPos:wide;
}
// One axis of the surface tangent frame. A pair of neighbours is only
// trusted when it continues through the centre (cosine >= .85): at the foot
// of a wall the sample toward the wall climbs while the other runs along the
// ground, and averaging them tilted the ground normal into the wall's,
// painting a bright GI/shadow-bias strip along every cliff base. Fall back
// from the wide pair to the one-pixel pair, then to the one-sided difference
// on the side that looks like the centre in the scene copy, the closer side in depth when the colours do not tell (the receiver's own surface).
// A silhouette pixel (no consistent pair even at one pixel) reports
// confidence 0 in the normal buffer's w: its one-sided normal may belong to
// the neighbouring surface. Suppress uncertain positive bounce there; shadow
// receivers validate their geometry separately. Retain ambient occlusion:
// skipping negative GI exposed
// a bright native-ambient seam where the ground touches a wall.
// c, step: the centre texel and the one-pixel step of this axis. The one-sided tangent follows the side whose colour looks
// more like the centre (sand next to a dark face turned away at a wall foot), not the closer side in depth: at a wall foot
// the closer side is the wall for a ground pixel and the ground for a wall pixel alike. When the two sides' relative
// colour differences (the composite tent's metric) differ by less than .05, the closer side in depth decides.
float3 tangentAxis(float3 p,float3 a,float3 b,float3 a1,float3 b1,float2 c,float2 step,inout float confidence) {
    float3 da=p-a,db=b-p;
    if(dot(da,db)*rsqrt(max(dot(da,da)*dot(db,db),1e-12))>=.85)return b-a;
    da=p-a1;db=b1-p;
    if(dot(da,db)*rsqrt(max(dot(da,da)*dot(db,db),1e-12))>=.85)return b1-a1;
    confidence=0;
    // Scene luminance of the centre, b1 and a1 pixels through one fetch site (each tex2Dlod call site costs registers, this pass has 32).
    // Unrolled: lumas=(L(c),L(c+step),L(c-step)); the lane mask float3(k==0,k==1,k==2) adds each into its own component.
    float3 lumas=0;
    [loop]for(int k=0;k<3;++k)lumas+=dot(tex2Dlod(Scene,float4(k==0?c:(k==1?min(c+step,1-.5*ImageClip.xy):max(c-step,.5*ImageClip.xy)),0,0)).rgb,float3(.299,.587,.114))*float3(k==0,k==1,k==2);
    float2 colourGap=abs(lumas.x-lumas.yz)/(lumas.x+lumas.yz+.02); // the composite tent's relative luminance difference, per side
    float gap=colourGap.x-colourGap.y;
    return (abs(gap)>.05?gap<0:abs(b1.z-p.z)<abs(a1.z-p.z))?db:da;
}
sampler2D ModelNormals : register(s13); // skinned view normal.xyz and linear depth.w; WorldNormals only
float4 ModelNormalInfo : register(c79);
float4 worldNormalValue(float2 c,float d,float z) {
    if(d>=.99999)return float4(0,0,1,z);
    [branch]if(ModelNormalInfo.x>.5){
        float4 model=tex2Dlod(ModelNormals,float4(c,0,0));
        float length2=dot(model.xyz,model.xyz);
        [branch]if(model.w>0&&abs(model.w-z)<max(.006,z*.0015)&&length2>.5&&length2<1.5){
            float3 n=normalize(model.xyz);
            return float4(n.x*InverseView[0].xyz+n.y*InverseView[1].xyz+n.z*InverseView[2].xyz,1);
        }
    }
    float3 p=viewPositionDistance(c,z);
    // lo/hi (the valid depth texel centre range) are recomputed where used: the pass is at its 32-register limit.
    // Baseline: 1.5 world units projected to pixels at this distance, 2..40 px.
    float pixels=clamp(1.5*Projection.x*ScreenSize.x*.5/z,2,40);
    float confidence=1;
    float3 tx=0; // the x tangent, then cross(x tangent, y tangent): one register triple less (32-register limit)
    // Share the two-axis body to stay within the SM3 static instruction budget.
    [loop]for(int axis=0;axis<2;++axis){
        float2 step=axis==0?float2(ImageClip.x,0):float2(0,ImageClip.y);
        float2 left=max(c-step,.5*ImageClip.xy),right=min(c+step,1-.5*ImageClip.xy);
        float3 a1=viewPosition(left,normalizedDepth(left));
        float3 b1=viewPosition(right,normalizedDepth(right));
        float3 a=a1,b=b1;
        [loop]for(int side=0;side<2;++side){ // one inlined copy of wideNeighbour for both sides (the pass is at its 32-register limit)
            float3 wide=wideNeighbour(c,side==0?-step:step,pixels,p,z,side==0?a1:b1);
            if(side==0)a=wide;else b=wide;
        }
        // r77: the wide pair only when BOTH wide samples pass. One wide sample on a low object's
        // top against the other side's 1-px neighbour spans the whole secant (a tilt of up to
        // 18 degrees) beside the object; the 1-px pair keeps flat ground flat.
        if(all(a==a1)||all(b==b1)){a=a1;b=b1;}
        a=tangentAxis(p,a,b,a1,b1,c,step,confidence); // a reused for the tangent
        tx=axis==0?a:cross(tx,a);
    }
    float3 n=tx*rsqrt(max(dot(tx,tx),1e-12));
    n=dot(n,-p)<0?-n:n;
    float3 world=n.x*InverseView[0].xyz+n.y*InverseView[1].xyz+n.z*InverseView[2].xyz;
    return float4(world,confidence);
}
struct NormalDistanceOutput {float4 normal:COLOR0;float4 distance:COLOR1;};
NormalDistanceOutput WorldNormals(float2 uv:TEXCOORD0) {
    float2 c=halfDepthUV(uv);float d=normalizedDepth(c);float z=viewDistance(d);
    NormalDistanceOutput o;o.normal=worldNormalValue(c,d,z);o.distance=z.xxxx;
    return o;
}

// No distance MRT is bound when no light source is drawn.
float4 WorldNormalsNoDistance(float2 uv:TEXCOORD0):COLOR0 {
    float2 c=halfDepthUV(uv);float d=normalizedDepth(c);
    return worldNormalValue(c,d,viewDistance(d));
}

sampler2D WorldNormalImage : register(s14);

float4 WorldNormalsView(float2 uv:TEXCOORD0):COLOR0 {
    float3 normal=tex2D(WorldNormalImage,uv).xyz;
    float3 color=normalizedDepthFlat(depthUV(uv))>=.99999?float3(0,0,0):saturate(normal*.5+.5);
    return float4(color,1);
}
