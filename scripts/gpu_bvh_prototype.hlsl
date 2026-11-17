// Standalone CS5 experiment; not part of the production D3D9 shader bundle.
struct Vertex { float3 p; float3 n; float2 uv; };
struct Material { float3 albedo; float cutoff; uint width, height, offset, addressU; uint addressV, opaque, pad0, pad1; };
struct Box { float3 lo; float3 hi; };
struct Ray { float3 o; float minT; float3 d; float maxT; };
struct Hit { float distance; uint triId; float u,v; float3 normal; uint back; float3 geometric; uint error; float3 albedo; uint pad; };
struct ProbeIn { float3 p; uint seed; };
struct ProbeOut { float4 sh[4]; float2 moments[6]; uint samples, back, valid, error; };
cbuffer Params : register(b0) {
 uint n, padded, leaves, count;
 uint stage, jump, sceneRoot, rayCount;
 uint leafSize, anyHit, maxBounces, unused;
 float4 sun; float4 sunRGB; float4 sky; float4 up;
};
StructuredBuffer<Vertex> vertices : register(t0);
StructuredBuffer<uint4> triangles : register(t1);
StructuredBuffer<Material> materials : register(t2);
StructuredBuffer<uint> texels : register(t3);
StructuredBuffer<float> srgb : register(t4);
StructuredBuffer<Ray> rays : register(t5);
StructuredBuffer<ProbeIn> probes : register(t6);
RWStructuredBuffer<uint2> keys : register(u0);
globallycoherent RWStructuredBuffer<Box> nodes : register(u1);
RWStructuredBuffer<Box> reductions : register(u2);
RWStructuredBuffer<Hit> hits : register(u3);
RWStructuredBuffer<ProbeOut> output : register(u4);
globallycoherent RWStructuredBuffer<uint4> links : register(u5); // children, parent, completion count
RWStructuredBuffer<Material> materialOutput : register(u6);
groupshared float3 lows[256], highs[256];
groupshared uint2 sorting[256];
groupshared uint alphaMinimum[256];
[numthreads(256,1,1)] void MaterialOpacity(uint3 group:SV_GroupID,uint lane:SV_GroupIndex){
 Material m=materialOutput[group.x];uint value=255;
 for(uint i=lane;i<m.width*m.height;i+=256)value=min(value,texels[m.offset+i]>>24);
 alphaMinimum[lane]=value;GroupMemoryBarrierWithGroupSync();
 for(uint s=128;s;s>>=1){if(lane<s)alphaMinimum[lane]=min(alphaMinimum[lane],alphaMinimum[lane+s]);GroupMemoryBarrierWithGroupSync();}
 if(!lane){m.opaque=(!m.width||!m.height)||float(alphaMinimum[0])/255*(1-1e-5)>=m.cutoff;materialOutput[group.x]=m;}
}
Box emptyBox() { Box b; b.lo=3.402823e38; b.hi=-3.402823e38; return b; }
Box merge(Box a,Box b) { a.lo=min(a.lo,b.lo); a.hi=max(a.hi,b.hi); return a; }
Box triangleBox(uint i) { uint3 t=triangles[i].xyz; Box b; b.lo=min(vertices[t.x].p,min(vertices[t.y].p,vertices[t.z].p)); b.hi=max(vertices[t.x].p,max(vertices[t.y].p,vertices[t.z].p)); return b; }
[numthreads(256,1,1)] void SceneBounds(uint3 id:SV_DispatchThreadID,uint3 group:SV_GroupID,uint lane:SV_GroupIndex) {
 Box b=emptyBox(); if(id.x<n)b=triangleBox(id.x);
 lows[lane]=b.lo; highs[lane]=b.hi; GroupMemoryBarrierWithGroupSync();
 for(uint s=128;s;s>>=1){if(lane<s){lows[lane]=min(lows[lane],lows[lane+s]);highs[lane]=max(highs[lane],highs[lane+s]);}GroupMemoryBarrierWithGroupSync();}
 if(!lane){b.lo=lows[0];b.hi=highs[0];reductions[group.x]=b;}
}
[numthreads(256,1,1)] void ReduceBounds(uint3 id:SV_DispatchThreadID,uint3 group:SV_GroupID,uint lane:SV_GroupIndex) {
 Box b=emptyBox();if(id.x<count)b=reductions[stage+id.x];lows[lane]=b.lo;highs[lane]=b.hi;GroupMemoryBarrierWithGroupSync();
 for(uint s=128;s;s>>=1){if(lane<s){lows[lane]=min(lows[lane],lows[lane+s]);highs[lane]=max(highs[lane],highs[lane+s]);}GroupMemoryBarrierWithGroupSync();}
 if(!lane){b.lo=lows[0];b.hi=highs[0];reductions[jump+group.x]=b;}
}
uint expandBits(uint v){v=(v|(v<<16))&0x030000ff;v=(v|(v<<8))&0x0300f00f;v=(v|(v<<4))&0x030c30c3;return(v|(v<<2))&0x09249249;}
[numthreads(256,1,1)] void Morton(uint3 id:SV_DispatchThreadID) {
 uint i=id.x;if(i>=padded)return;uint2 k=uint2(0xffffffff,0xffffffff);
 if(i<n){uint3 t=triangles[i].xyz;float3 c=(vertices[t.x].p+vertices[t.y].p+vertices[t.z].p)/3;Box b=reductions[sceneRoot];
 uint3 q=(uint3)(saturate((c-b.lo)/max(b.hi-b.lo,1e-10))*1023);k=uint2(expandBits(q.x)|(expandBits(q.y)<<1)|(expandBits(q.z)<<2),i);}
 keys[i]=k;
}
bool greaterKey(uint2 a,uint2 b){return a.x>b.x||(a.x==b.x&&a.y>b.y);}
[numthreads(256,1,1)] void SortLocal(uint3 id:SV_DispatchThreadID,uint lane:SV_GroupIndex) {
 sorting[lane]=keys[id.x];GroupMemoryBarrierWithGroupSync();
 for(uint k=2;k<=256;k<<=1)for(uint j=k>>1;j;j>>=1){uint2 a=sorting[lane],b=sorting[lane^j];bool small=((id.x&k)==0)==((lane&j)==0);
 GroupMemoryBarrierWithGroupSync();sorting[lane]=(greaterKey(a,b)==small)?b:a;GroupMemoryBarrierWithGroupSync();}
 keys[id.x]=sorting[lane];
}
[numthreads(256,1,1)] void SortGlobal(uint3 id:SV_DispatchThreadID) {
 uint i=id.x,other=i^jump;if(i>=padded||other<=i)return;uint2 a=keys[i],b=keys[other];
 if(greaterKey(a,b)==((i&stage)==0)){keys[i]=b;keys[other]=a;}
}
[numthreads(256,1,1)] void MergeLocal(uint3 id:SV_DispatchThreadID,uint lane:SV_GroupIndex) {
 sorting[lane]=keys[id.x];GroupMemoryBarrierWithGroupSync();
 for(uint j=128;j;j>>=1){uint2 a=sorting[lane],b=sorting[lane^j];bool small=((id.x&stage)==0)==((lane&j)==0);
 GroupMemoryBarrierWithGroupSync();sorting[lane]=(greaterKey(a,b)==small)?b:a;GroupMemoryBarrierWithGroupSync();}
 keys[id.x]=sorting[lane];
}
[numthreads(256,1,1)] void LeafBounds(uint3 id:SV_DispatchThreadID) {
 uint i=id.x;if(i>=leaves)return;Box b=emptyBox();for(uint j=0;j<leafSize;++j){uint k=i*leafSize+j;if(k<n)b=merge(b,triangleBox(keys[k].y));}
 // Conservative traversal bounds, including nearly parallel and equal-distance rays.
 float3 e=max(abs(b.lo),abs(b.hi))*1e-6+1e-6;if(i*leafSize<n){b.lo-=e;b.hi+=e;}nodes[leaves+i]=b;
}
[numthreads(256,1,1)] void InnerBounds(uint3 id:SV_DispatchThreadID) {uint i=id.x;if(i>=stage)return;uint node=stage+i;nodes[node]=merge(nodes[node*2],nodes[node*2+1]);}
// Binary radix topology by common Morton prefix (Karras, HPG 2012).
int prefix(int i,int j){if(j<0||j>=int(leaves))return -1;if(i==j)return 64;
 uint x=keys[uint(i)*leafSize].x^keys[uint(j)*leafSize].x;
 return x?31-int(firstbithigh(x)):63-int(firstbithigh(uint(i^j)));}
[numthreads(256,1,1)] void RadixTopology(uint3 id:SV_DispatchThreadID){int i=int(id.x);if(i>=int(leaves)-1)return;
 int direction=prefix(i,i+1)-prefix(i,i-1)>=0?1:-1,minimum=prefix(i,i-direction),maximum=2;
 while(prefix(i,i+maximum*direction)>minimum)maximum*=2;
 int length=0;for(int step=maximum/2;step;step/=2)if(prefix(i,i+(length+step)*direction)>minimum)length+=step;
 int end=i+length*direction,first=min(i,end),last=max(i,end),common=prefix(first,last),split=first,splitStep=last-first;
 do{splitStep=(splitStep+1)/2;int candidate=split+splitStep;if(candidate<last&&prefix(first,candidate)>common)split=candidate;}while(splitStep>1);
 uint left=split==first?leaves-1+uint(split):uint(split),right=split+1==last?leaves+uint(split):uint(split+1);
 links[i].x=left;links[i].y=right;links[i].w=0;links[left].z=uint(i);links[right].z=uint(i);
}
[numthreads(256,1,1)] void ClearVisits(uint3 id:SV_DispatchThreadID){if(id.x<leaves-1)links[id.x].w=0;}
[numthreads(256,1,1)] void RadixBounds(uint3 id:SV_DispatchThreadID){uint i=id.x;if(i>=leaves)return;Box b=emptyBox();
 for(uint j=0;j<leafSize;++j){uint k=i*leafSize+j;if(k<n)b=merge(b,triangleBox(keys[k].y));}
 if(i*leafSize<n){float3 e=max(abs(b.lo),abs(b.hi))*1e-6+1e-6;b.lo-=e;b.hi+=e;}uint node=leaves-1+i;nodes[node]=b;
 [loop]while(node){DeviceMemoryBarrier();uint parent=links[node].z,old;InterlockedAdd(links[parent].w,1,old);if(old==0)return;
 DeviceMemoryBarrier();uint2 children=links[parent].xy;nodes[parent]=merge(nodes[children.x],nodes[children.y]);node=parent;}
}
float address(float v,uint mode){if(mode==3)return saturate(v);if(mode==2){float p=v-2*floor(v*.5);return p<=1?p:2-p;}return frac(v);}
uint texelIndex(int i,uint size,uint mode){if(mode==1)return uint(i+int(size))%size;return uint(clamp(i,0,int(size)-1));}
float4 sampleMaterial(Material m,float2 uv) {
 if(!m.width||!m.height)return float4(m.albedo,1);
 precise float2 q=float2(address(uv.x,m.addressU),address(uv.y,m.addressV))*float2(m.width,m.height)-.5;
 int2 base=(int2)floor(q);precise float2 f=frac(q);precise float4 s=0;
 [unroll]for(int y=0;y<2;++y)[unroll]for(int x=0;x<2;++x){uint ix=texelIndex(base.x+x,m.width,m.addressU),iy=texelIndex(base.y+y,m.height,m.addressV);
 uint p=texels[m.offset+iy*m.width+ix];float4 c=float4(srgb[p&255],srgb[(p>>8)&255],srgb[(p>>16)&255],float(p>>24)/255);
 s+=c*(x?f.x:1-f.x)*(y?f.y:1-f.y);}s.rgb*=m.albedo;return s;
}
float dot3(float3 a,float3 b){precise float3 p=a*b;precise float v=(p.x+p.y)+p.z;return v;}
float3 cross3(float3 a,float3 b){precise float3 v=a.yzx*b.zxy-a.zxy*b.yzx;return v;}
float3 safeNormalize(float3 v){precise float d=dot3(v,v);precise float len=sqrt(d);precise float inv=1/len;precise float3 result=v*inv;return d>1e-20&&isfinite(d)?result:0;}
bool boxHit(Box b,Ray r,float upper,out float entry){entry=r.minT;float exit=upper;
 [unroll]for(uint i=0;i<3;++i){if(abs(r.d[i])<1e-12){if(r.o[i]<b.lo[i]||r.o[i]>b.hi[i])return false;}
 else{float a=(b.lo[i]-r.o[i])/r.d[i],c=(b.hi[i]-r.o[i])/r.d[i];entry=max(entry,min(a,c));exit=min(exit,max(a,c));}}return entry<=exit;}
Hit trace(Ray r,bool occlusion) {
 Hit h=(Hit)0;h.distance=r.maxT;h.triId=0xffffffff;r.d=safeNormalize(r.d);
 if(!n||!all(isfinite(r.o))||dot3(r.d,r.d)<.5||!isfinite(r.minT)||!isfinite(r.maxT)||r.minT<0||r.maxT<=r.minT)return h;
 uint stack[64];float entries[64];uint top=0;stack[top]=unused?0:1;entries[top++]=r.minT;
 [loop]while(top){--top;uint node=stack[top];float entry=entries[top];float upper=h.distance+abs(h.distance)*1e-6;
 if(entry>upper)continue;if(node>=(unused?leaves-1:leaves)){uint start=(node-(unused?leaves-1:leaves))*leafSize;uint limit=leafSize;
 [loop]for(uint j=0;j<limit&&start+j<n;++j){uint ti=keys[start+j].y;uint4 t=triangles[ti];Vertex a=vertices[t.x],b=vertices[t.y],c=vertices[t.z];
 float3 ab=b.p-a.p,ac=c.p-a.p,p=cross3(r.d,ac);float determinant=dot3(ab,p);if(abs(determinant)<1e-10)continue;
 float3 area=cross3(ab,ac);if(dot3(area,area)<=1e-16)continue;
 float inverse=1/determinant;float3 s=r.o-a.p;float u=dot3(s,p)*inverse;if(u<0||u>1)continue;float3 q=cross3(s,ab);float v=dot3(r.d,q)*inverse;
 if(v<0||u+v>1)continue;float d=dot3(ac,q)*inverse;if(d<r.minT||d>h.distance||d==h.distance&&(h.triId==0xffffffff||ti>=h.triId))continue;
 Material m=materials[t.w];precise float w=1-u-v;precise float2 uv=a.uv*w+b.uv*u+c.uv*v;if(!m.opaque&&sampleMaterial(m,uv).a<m.cutoff)continue;
 h.distance=d;h.triId=ti;h.u=u;h.v=v;if(occlusion)return h;}
 }else{uint2 children=unused?links[node].xy:uint2(node*2,node*2+1);float left,right;bool l=boxHit(nodes[children.x],r,upper,left),rr=boxHit(nodes[children.y],r,upper,right);
 if(top+uint(l)+uint(rr)>64){h.error=1;return h;}
 if(l&&rr){bool nearLeft=left<right;stack[top]=nearLeft?children.y:children.x;entries[top++]=nearLeft?right:left;stack[top]=nearLeft?children.x:children.y;entries[top++]=nearLeft?left:right;}
 else if(l){stack[top]=children.x;entries[top++]=left;}else if(rr){stack[top]=children.y;entries[top++]=right;}}
 }
 if(h.triId!=0xffffffff){uint4 t=triangles[h.triId];Vertex a=vertices[t.x],b=vertices[t.y],c=vertices[t.z];float w=1-h.u-h.v;
 float3 gn=safeNormalize(cross3(b.p-a.p,c.p-a.p)),normal=safeNormalize(a.n*w+b.n*h.u+c.n*h.v);
 float3 outward=dot3(normal,normal)>.5?normal:gn;h.back=dot3(outward,r.d)>0;if(dot3(gn,r.d)>0)gn=-gn;if(dot3(normal,normal)<.5)normal=gn;
 if(dot3(normal,gn)<0)normal=-normal;if(dot3(normal,r.d)>-.001)normal=gn;h.normal=normal;h.geometric=gn;
 h.albedo=sampleMaterial(materials[t.w],a.uv*w+b.uv*h.u+c.uv*h.v).rgb;}
 return h;
}
[numthreads(64,1,1)] void TraceRays(uint3 id:SV_DispatchThreadID){if(id.x<count)hits[id.x]=trace(rays[id.x],anyHit!=0);}
uint random(inout uint s){s^=s<<13;s^=s>>17;s^=s<<5;return s;}
float randomFloat(inout uint s){return float(random(s)>>8)/16777216;}
float3 cosineDirection(float3 n,inout uint rng){float u=randomFloat(rng),phi=6.28318530718*randomFloat(rng),r=sqrt(u);
 float3 helper=abs(n.z)<.999?float3(0,0,1):float3(0,1,0),t=safeNormalize(cross3(helper,n)),b=cross3(n,t);
 return safeNormalize(t*(r*cos(phi))+b*(r*sin(phi))+n*sqrt(max(0,1-u)));}
// First GI feasibility gate: production sun/sky transport, alpha and normal rules.
// Points, extra directions, aurora, moving overlays and relight caches are not implemented.
float3 transport(Ray ray,inout uint rng,out float distance,out uint back,out uint error){
 float3 throughput=1,radiance=0;distance=ray.maxT;back=0;error=0;
 [loop]for(uint bounce=0;bounce<=maxBounces;++bounce){Hit h=trace(ray,false);error|=h.error;if(!bounce){distance=h.distance;back=h.back;}
 if(h.triId==0xffffffff){if(dot3(ray.d,up.xyz)>0)radiance+=throughput*sky.xyz;break;}if(bounce==maxBounces)break;
 float3 offset=ray.o+safeNormalize(ray.d)*h.distance+h.geometric*sun.w;
 float cosine=max(0,dot3(h.normal,sun.xyz));
 if(cosine>0&&dot3(h.geometric,sun.xyz)>0&&dot3(sunRGB.xyz,sunRGB.xyz)>=1e-16){Ray shadow;shadow.o=offset;shadow.minT=.0005;shadow.d=sun.xyz;shadow.maxT=ray.maxT;
 Hit blocker=trace(shadow,true);error|=blocker.error;if(blocker.triId==0xffffffff)radiance+=throughput*h.albedo*sunRGB.xyz*(cosine/3.14159265359);}
 throughput*=h.albedo;if(max(throughput.x,max(throughput.y,throughput.z))<1e-6)break;ray.d=cosineDirection(h.normal,rng);if(dot3(ray.d,h.geometric)<=0)break;ray.o=offset;
 }return max(radiance,0);
}
[numthreads(64,1,1)] void SolveProbes(uint3 id:SV_DispatchThreadID){if(id.x>=count)return;ProbeIn p=probes[id.x];ProbeOut o=(ProbeOut)0;
 float weights[6]={0,0,0,0,0,0},sums[6]={0,0,0,0,0,0},squares[6]={0,0,0,0,0,0};float closest=up.w;
 uint orient=p.seed^0xa511e9b3;float azimuth=randomFloat(orient)*6.28318530718;float s=sin(azimuth),c=cos(azimuth);
 [loop]for(uint i=0;i<rayCount;++i){float z=1-2*(float(i)+.5)/rayCount,r=sqrt(max(0,1-z*z)),phi=float(i)*2.39996322972865332;
 float3 d=float3(r*cos(phi),r*sin(phi),z);d=float3(d.x*c-d.y*s,d.x*s+d.y*c,d.z);uint rng=p.seed^((i+1)*0x9e3779b9);if(!rng)rng=0x9e3779b9;
 Ray ray;ray.o=p.p;ray.minT=.0005;ray.d=d;ray.maxT=up.w;float distance;uint back,error;float3 color=transport(ray,rng,distance,back,error);
 o.back+=back;o.error|=error;closest=min(closest,distance);float4 sh=float4(.2820947918,.4886025119*d);
 [unroll]for(uint j=0;j<4;++j)o.sh[j].xyz+=color*(sh[j]*(12.56637061436/rayCount));
 [unroll]for(uint axis=0;axis<3;++axis){uint j=axis*2+(d[axis]<0);float w=abs(d[axis]);w*=w;w*=w;w*=w;weights[j]+=w;sums[j]+=w*distance;squares[j]+=w*distance*distance;}}
 [unroll]for(uint j=0;j<6;++j)o.moments[j]=weights[j]>1e-10?float2(sums[j],squares[j])/weights[j]:float2(up.w,up.w*up.w);
 o.samples=rayCount;o.valid=o.back<=rayCount/4&&closest>=.02;if(!o.valid){[unroll]for(uint k=0;k<4;++k)o.sh[k]=0;}output[id.x]=o;
}
groupshared float3 rayColors[32],rayDirections[32];
groupshared float rayDistances[32];
groupshared uint rayBack[32],rayErrors[32];
// One 32-ray production-sized probe per group. No repeated traversal on one lane.
[numthreads(32,1,1)] void SolveParallel(uint3 group:SV_GroupID,uint lane:SV_GroupIndex){
 ProbeIn p=probes[group.x];uint orient=p.seed^0xa511e9b3;float azimuth=randomFloat(orient)*6.28318530718;
 float z=1-2*(float(lane)+.5)/32,r=sqrt(max(0,1-z*z)),phi=float(lane)*2.39996322972865332;
 float3 d=float3(r*cos(phi),r*sin(phi),z);float s=sin(azimuth),c=cos(azimuth);d=float3(d.x*c-d.y*s,d.x*s+d.y*c,d.z);
 uint rng=p.seed^((lane+1)*0x9e3779b9);if(!rng)rng=0x9e3779b9;Ray ray;ray.o=p.p;ray.minT=.0005;ray.d=d;ray.maxT=up.w;
 float distance;uint back,error;rayColors[lane]=transport(ray,rng,distance,back,error);rayDirections[lane]=d;rayDistances[lane]=distance;rayBack[lane]=back;rayErrors[lane]=error;
 GroupMemoryBarrierWithGroupSync();
 if(!lane){ProbeOut o=(ProbeOut)0;float weights[6]={0,0,0,0,0,0},sums[6]={0,0,0,0,0,0},squares[6]={0,0,0,0,0,0};float closest=up.w;
 [loop]for(uint i=0;i<32;++i){float3 color=rayColors[i],d=rayDirections[i];float distance=rayDistances[i];o.back+=rayBack[i];o.error|=rayErrors[i];closest=min(closest,distance);
 float4 sh=float4(.2820947918,.4886025119*d);[unroll]for(uint j=0;j<4;++j)o.sh[j].xyz+=color*(sh[j]*(12.56637061436/32));
 [unroll]for(uint axis=0;axis<3;++axis){uint j=axis*2+(d[axis]<0);float w=abs(d[axis]);w*=w;w*=w;w*=w;weights[j]+=w;sums[j]+=w*distance;squares[j]+=w*distance*distance;}}
 [unroll]for(uint j=0;j<6;++j)o.moments[j]=weights[j]>1e-10?float2(sums[j],squares[j])/weights[j]:float2(up.w,up.w*up.w);
 o.samples=32;o.valid=o.back<=8&&closest>=.02;if(!o.valid){[unroll]for(uint k=0;k<4;++k)o.sh[k]=0;}output[group.x]=o;}
}
