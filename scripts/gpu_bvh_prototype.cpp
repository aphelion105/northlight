// Hardware-only x86 feasibility benchmark. No production renderer/device changes.
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <bcrypt.h>
#include "world_gi.h"
#include "gi_solve_pool.h"
#include "scene_source_cache.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <limits>
#include <memory>
#include <stdexcept>
#include <vector>
using namespace NorthlightGI;
using Clock=std::chrono::steady_clock;
static bool measureRejected=false;
static bool negativeBounds=false;
static unsigned parityFailures=0;
static void parity(bool ok,const char* message){if(!ok){++parityFailures;if(!measureRejected)throw std::runtime_error(message);}}
static double elapsed(Clock::time_point t){return std::chrono::duration<double,std::milli>(Clock::now()-t).count();}
static void require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
static void check(HRESULT hr){if(FAILED(hr)){char b[64];std::snprintf(b,sizeof b,"D3D HRESULT %08lx",(unsigned long)hr);throw std::runtime_error(b);}}
static double processMs(){FILETIME c,e,k,u;check(GetProcessTimes(GetCurrentProcess(),&c,&e,&k,&u)?S_OK:E_FAIL);ULARGE_INTEGER a,b;a.LowPart=k.dwLowDateTime;a.HighPart=k.dwHighDateTime;b.LowPart=u.dwLowDateTime;b.HighPart=u.dwHighDateTime;return double(a.QuadPart+b.QuadPart)*.0001;}
static uint64_t processCycles(){ULONG64 cycles=0;require(QueryProcessCycleTime(GetCurrentProcess(),&cycles),"Cannot query CPU cycles");return cycles;}
template<class T>struct Release {void operator()(T* p)const{if(p)p->Release();}};
template<class T>using Com=std::unique_ptr<T,Release<T>>;
// Receive COM out-parameters into a raw local, then transfer ownership.
template<class T,class F>static Com<T> makeCom(F f){T* p=nullptr;check(f(&p));return Com<T>(p);}
struct Buffer {Com<ID3D11Buffer> buffer;Com<ID3D11ShaderResourceView> srv;Com<ID3D11UnorderedAccessView> uav;UINT bytes=0,stride=0;};
struct Box {Vec3 lo,hi;};
struct Ray {Vec3 o;float minT;Vec3 d;float maxT;};
struct Hit {float distance;uint32_t triangle;float u,v;Vec3 normal;uint32_t back;Vec3 geometric;uint32_t error;Vec3 albedo;uint32_t pad;};
struct Material {Vec3 albedo;float cutoff;uint32_t width,height,offset,addressU,addressV,opaque,pad0=0,pad1=0;};
struct ProbeIn {Vec3 p;uint32_t seed;};
struct ProbeOut {float sh[4][4];DistanceMoment moments[6];uint32_t samples,back,valid,error;};
struct Params {uint32_t n=0,padded=256,leaves=1,count=0,stage=0,jump=0,sceneRoot=0,rayCount=32,leafSize=4,anyHit=0,maxBounces=3,unused=0;
 float sun[4]={},sunRGB[4]={},sky[4]={},up[4]={};};
static_assert(sizeof(Params)==112&&sizeof(Material)==48&&sizeof(Hit)==64&&sizeof(ProbeOut)==128,"Shader ABI");
struct Metric {double wall=0,cpu=0,submit=0,gpu=0;uint64_t cycles=0;};
static void metric(const char* phase,const Metric& m,int round,int leaf,unsigned count){std::printf("{\"phase\":\"%s\",\"round\":%d,\"leaf\":%d,\"count\":%u,\"wall_ms\":%.6f,\"cpu_ms\":%.6f,\"submit_ms\":%.6f,\"gpu_ms\":%.6f,\"cpu_cycles\":%llu}\n",phase,round,leaf,count,m.wall,m.cpu,m.submit,m.gpu,(unsigned long long)m.cycles);std::fflush(stdout);}
class GPU {
public:
 Com<ID3D11Device> d;Com<ID3D11DeviceContext> c;Com<ID3D11Buffer> constants;
 std::vector<Com<ID3D11ComputeShader>> shaders;
 Com<ID3D11Query> disjoint,start,stop;Params p;uint64_t deviceBytes=0,stagingBytes=0;
 enum {Bounds,Reduce,Morton,SortLocal,SortGlobal,MergeLocal,Leaves,Inner,Trace,Solve,Parallel,Topology,Clear,RadixBounds,Opacity};
 GPU(const std::string& dir){
  auto factory=makeCom<IDXGIFactory1>([](IDXGIFactory1** v){return CreateDXGIFactory1(__uuidof(IDXGIFactory1),(void**)v);});
  IDXGIAdapter1* raw=nullptr;check(factory->EnumAdapters1(0,&raw));Com<IDXGIAdapter1> adapter(raw);DXGI_ADAPTER_DESC1 desc{};check(adapter->GetDesc1(&desc));
  require(!(desc.Flags&DXGI_ADAPTER_FLAG_SOFTWARE),"Hardware adapter required");char name[256];WideCharToMultiByte(CP_UTF8,0,desc.Description,-1,name,sizeof name,nullptr,nullptr);
  std::printf("{\"adapter\":\"%s\",\"vendor\":%u,\"device\":%u,\"luid_low\":%lu,\"luid_high\":%ld,\"dxgi_dedicated_bytes_x86\":%llu}\n",name,desc.VendorId,desc.DeviceId,desc.AdapterLuid.LowPart,desc.AdapterLuid.HighPart,(unsigned long long)desc.DedicatedVideoMemory);
  ID3D11Device* device=nullptr;ID3D11DeviceContext* context=nullptr;D3D_FEATURE_LEVEL got,level=D3D_FEATURE_LEVEL_11_0;
  check(D3D11CreateDevice(adapter.get(),D3D_DRIVER_TYPE_UNKNOWN,nullptr,D3D11_CREATE_DEVICE_SINGLETHREADED,&level,1,D3D11_SDK_VERSION,&device,&got,&context));d.reset(device);c.reset(context);require(got>=level,"CS5 required");
  const char* entries[]={"SceneBounds","ReduceBounds","Morton","SortLocal","SortGlobal","MergeLocal","LeafBounds","InnerBounds","TraceRays","SolveProbes","SolveParallel","RadixTopology","ClearVisits","RadixBounds","MaterialOpacity"};
  for(auto name:entries){std::ifstream f(dir+"/"+name+".bin",std::ios::binary);require(bool(f),"Missing shader");std::vector<char>b((std::istreambuf_iterator<char>(f)),{});shaders.push_back(makeCom<ID3D11ComputeShader>([&](ID3D11ComputeShader** s){return d->CreateComputeShader(b.data(),b.size(),nullptr,s);}));}
  D3D11_BUFFER_DESC bd{};bd.ByteWidth=sizeof p;bd.Usage=D3D11_USAGE_DEFAULT;bd.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
  constants=makeCom<ID3D11Buffer>([&](ID3D11Buffer** b){return d->CreateBuffer(&bd,nullptr,b);});auto cb=constants.get();c->CSSetConstantBuffers(0,1,&cb);
  auto query=[&](D3D11_QUERY type){D3D11_QUERY_DESC q{type,0};return makeCom<ID3D11Query>([&](ID3D11Query** v){return d->CreateQuery(&q,v);});};
  disjoint=query(D3D11_QUERY_TIMESTAMP_DISJOINT);start=query(D3D11_QUERY_TIMESTAMP);stop=query(D3D11_QUERY_TIMESTAMP);
 }
 Buffer buffer(UINT count,UINT stride,bool writable=false){Buffer b;b.bytes=std::max(1u,count)*stride;b.stride=stride;D3D11_BUFFER_DESC desc{};
  desc.ByteWidth=b.bytes;desc.Usage=D3D11_USAGE_DEFAULT;desc.BindFlags=D3D11_BIND_SHADER_RESOURCE|(writable?D3D11_BIND_UNORDERED_ACCESS:0);desc.MiscFlags=D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;desc.StructureByteStride=stride;
  b.buffer=makeCom<ID3D11Buffer>([&](ID3D11Buffer** v){return d->CreateBuffer(&desc,nullptr,v);});
  D3D11_SHADER_RESOURCE_VIEW_DESC sv{};sv.Format=DXGI_FORMAT_UNKNOWN;sv.ViewDimension=D3D11_SRV_DIMENSION_BUFFER;sv.Buffer.NumElements=std::max(1u,count);
  b.srv=makeCom<ID3D11ShaderResourceView>([&](ID3D11ShaderResourceView** v){return d->CreateShaderResourceView(b.buffer.get(),&sv,v);});
  if(writable){D3D11_UNORDERED_ACCESS_VIEW_DESC uv{};uv.Format=DXGI_FORMAT_UNKNOWN;uv.ViewDimension=D3D11_UAV_DIMENSION_BUFFER;uv.Buffer.NumElements=std::max(1u,count);
   b.uav=makeCom<ID3D11UnorderedAccessView>([&](ID3D11UnorderedAccessView** v){return d->CreateUnorderedAccessView(b.buffer.get(),&uv,v);});}deviceBytes+=b.bytes;return b;
 }
 void upload(Buffer& b,const void* ptr,unsigned bytes=0){D3D11_BOX box{0,0,0,bytes?bytes:b.bytes,1,1};require(box.right<=b.bytes,"Upload exceeds buffer");c->UpdateSubresource(b.buffer.get(),0,&box,ptr,0,0);}
 void dispatch(unsigned shader,unsigned threads,unsigned group=256){c->UpdateSubresource(constants.get(),0,nullptr,&p,0,0);c->CSSetShader(shaders[shader].get(),nullptr,0);if(threads)c->Dispatch((threads+group-1)/group,1,1);}
 void wait(ID3D11Query* q,void* data,UINT size){auto t=Clock::now();HRESULT hr;while((hr=c->GetData(q,data,size,D3D11_ASYNC_GETDATA_DONOTFLUSH))==S_FALSE){require(elapsed(t)<30000,"GPU query timeout");Sleep(1);}check(hr);}
 template<class F>Metric timed(F commands){Metric m;auto t=Clock::now();double cpu=processMs();uint64_t cycles=processCycles();c->Begin(disjoint.get());c->End(start.get());commands();c->End(stop.get());c->End(disjoint.get());c->Flush();m.submit=elapsed(t);
  D3D11_QUERY_DATA_TIMESTAMP_DISJOINT dj{};wait(disjoint.get(),&dj,sizeof dj);UINT64 a,b;wait(start.get(),&a,sizeof a);wait(stop.get(),&b,sizeof b);require(!dj.Disjoint&&dj.Frequency,"Disjoint GPU timestamp");
  m.gpu=1000.*double(b-a)/double(dj.Frequency);m.wall=elapsed(t);m.cpu=processMs()-cpu;m.cycles=processCycles()-cycles;return m;
 }
 template<class T>std::vector<T> read(Buffer& b,unsigned count){D3D11_BUFFER_DESC desc{};b.buffer->GetDesc(&desc);desc.Usage=D3D11_USAGE_STAGING;desc.BindFlags=0;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;desc.MiscFlags=0;
  auto staging=makeCom<ID3D11Buffer>([&](ID3D11Buffer** v){return d->CreateBuffer(&desc,nullptr,v);});c->CopyResource(staging.get(),b.buffer.get());D3D11_MAPPED_SUBRESOURCE map{};check(c->Map(staging.get(),0,D3D11_MAP_READ,0,&map));
  std::vector<T> out(count);std::memcpy(out.data(),map.pData,count*sizeof(T));c->Unmap(staging.get(),0);stagingBytes=std::max<uint64_t>(stagingBytes,b.bytes);return out;
 }
};
struct Packed {
 std::vector<WorldVertex> vertices;std::vector<WorldTriangle> triangles;std::vector<Material> materials;std::vector<uint32_t> texels;std::array<float,256> srgb;
 explicit Packed(const WorldScene& s){vertices.reserve(s.vertices.size());triangles.reserve(s.triangles.size());materials.reserve(s.materials.size());size_t total=0;for(const auto& m:s.materials)total+=m.texels().size()/4;texels.reserve(total);for(const auto& v:s.vertices)vertices.push_back(v);for(const auto& t:s.triangles)triangles.push_back(t);
  for(const auto& m:s.materials){auto pixels=m.texels();Material p{{std::clamp(m.albedo.x,0.f,1.f),std::clamp(m.albedo.y,0.f,1.f),std::clamp(m.albedo.z,0.f,1.f)},m.alphaCutoff,m.width,m.height,uint32_t(texels.size()),m.addressU,m.addressV,1};
   if(!pixels.empty()){require(pixels.size()==uint64_t(m.width)*m.height*4,"Bad material");size_t offset=texels.size();texels.resize(offset+pixels.size()/4);std::memcpy(texels.data()+offset,pixels.data(),pixels.size());}
   else p.width=p.height=0;if(m.alphaCutoff>1)p.opaque=0;materials.push_back(p);}
  if(texels.empty())texels.push_back(0xffffffff);for(unsigned i=0;i<256;++i){float x=float(i)/255.f;srgb[i]=x<=.04045f?x/12.92f:std::pow((x+.055f)/1.055f,2.4f);}
 }
};
static void sceneIdentity(const Packed& s){BCRYPT_ALG_HANDLE algorithm=nullptr;BCRYPT_HASH_HANDLE hash=nullptr;
 require(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)>=0,"SHA256 provider");require(BCryptCreateHash(algorithm,&hash,nullptr,0,nullptr,0,0)>=0,"SHA256 hash");
 auto add=[&](const auto& values){uint64_t size=values.size()*sizeof(values[0]);require(size<=UINT32_MAX,"SHA256 input capacity");require(BCryptHashData(hash,(PUCHAR)&size,sizeof size,0)>=0,"SHA256 framing");if(size)require(BCryptHashData(hash,(PUCHAR)values.data(),ULONG(size),0)>=0,"SHA256 payload");};
 add(s.vertices);add(s.triangles);add(s.materials);add(s.texels);unsigned char digest[32];require(BCryptFinishHash(hash,digest,sizeof digest,0)>=0,"SHA256 finish");BCryptDestroyHash(hash);BCryptCloseAlgorithmProvider(algorithm,0);
 std::printf("{\"packed_scene_sha256\":\"");for(auto b:digest)std::printf("%02x",b);std::puts("\"}");
}
class Tree {
public:
 GPU& g;Buffer v,t,m,tex,srgb,keys,nodes,reduction,rays,hits,probes,outputs,links;
 unsigned padded=256,maxLeaves=1,reductionCount=0;
 Tree(GPU& gpu,const Packed& s):g(gpu){
  auto n=unsigned(s.triangles.size());while(padded<n)padded*=2;maxLeaves=std::max(1u,padded/2);
  for(unsigned k=(n+255)/256;k;k=k>1?(k+255)/256:0)reductionCount+=k;
  reductionCount=std::max(1u,reductionCount);
  v=g.buffer(s.vertices.size(),sizeof(WorldVertex));t=g.buffer(n,16);m=g.buffer(s.materials.size(),48,true);tex=g.buffer(s.texels.size(),4);srgb=g.buffer(256,4);
  keys=g.buffer(padded,8,true);nodes=g.buffer(maxLeaves*2,24,true);reduction=g.buffer(reductionCount,24,true);links=g.buffer(maxLeaves*2,16,true);
  rays=g.buffer(32768,32);hits=g.buffer(32768,64,true);probes=g.buffer(512,16);outputs=g.buffer(512,128,true);
  ID3D11ShaderResourceView* sv[]={v.srv.get(),t.srv.get(),m.srv.get(),tex.srv.get(),srgb.srv.get(),rays.srv.get(),probes.srv.get()};g.c->CSSetShaderResources(0,7,sv);
  ID3D11UnorderedAccessView* uv[]={keys.uav.get(),nodes.uav.get(),reduction.uav.get(),hits.uav.get(),outputs.uav.get(),links.uav.get()};g.c->CSSetUnorderedAccessViews(0,6,uv,nullptr);
  g.p.n=n;g.p.padded=padded;
 }
 void upload(const Packed& s){
  if(!s.vertices.empty())g.upload(v,s.vertices.data());if(!s.triangles.empty())g.upload(t,s.triangles.data());g.upload(m,s.materials.data());g.upload(tex,s.texels.data());g.upload(srgb,s.srgb.data());
  ID3D11ShaderResourceView* nullSrv=nullptr;g.c->CSSetShaderResources(2,1,&nullSrv);auto uav=m.uav.get();g.c->CSSetUnorderedAccessViews(6,1,&uav,nullptr);g.dispatch(GPU::Opacity,s.materials.size()*256);
  ID3D11UnorderedAccessView* nullUav=nullptr;g.c->CSSetUnorderedAccessViews(6,1,&nullUav,nullptr);auto srv=m.srv.get();g.c->CSSetShaderResources(2,1,&srv);
 }
 void build(unsigned leaf,bool radix=true){g.p.unused=radix;g.p.leafSize=leaf;g.p.leaves=radix?std::max(1u,(g.p.n+leaf-1)/leaf):std::max(1u,padded/leaf);g.dispatch(GPU::Bounds,g.p.n);
  unsigned offset=0,count=(g.p.n+255)/256,dest=count;
  while(count>1){g.p.count=count;g.p.stage=offset;g.p.jump=dest;g.dispatch(GPU::Reduce,count);offset=dest;count=(count+255)/256;dest+=count;}
  g.p.sceneRoot=offset;g.dispatch(GPU::Morton,padded);g.dispatch(GPU::SortLocal,padded);
  for(unsigned k=512;k<=padded;k*=2){g.p.stage=k;for(unsigned j=k/2;j>=256;j/=2){g.p.jump=j;g.dispatch(GPU::SortGlobal,padded);}g.dispatch(GPU::MergeLocal,padded);}
  if(radix)g.dispatch(GPU::Topology,g.p.leaves-1);refit();
 }
 void refit(){if(g.p.unused){g.dispatch(GPU::Clear,g.p.leaves-1);g.dispatch(GPU::RadixBounds,g.p.leaves);}else{g.dispatch(GPU::Leaves,g.p.leaves);for(unsigned n=g.p.leaves/2;n;n/=2){g.p.stage=n;g.dispatch(GPU::Inner,n);}}}
};
static WorldScene synthetic(unsigned kind){WorldScene s;WorldMaterial plain;plain.alphaCutoff=0;s.materials.push_back(plain);
 WorldMaterial cut;cut.width=3;cut.height=2;cut.rgba={255,64,32,0,64,255,64,255,32,64,255,0,255,255,255,255,128,32,64,0,64,128,32,255};s.materials.push_back(cut);
 auto mirrored=cut;mirrored.addressU=2;mirrored.addressV=3;s.materials.push_back(mirrored);
 unsigned rng=31;auto random=[&]{rng=rng*1664525+1013904223;return float(rng&65535)/256-128;};
 unsigned count=kind==0?0:kind==1?1:(kind==2||kind==7)?37:kind==3?12000:kind==4?350000:4096;
 for(unsigned i=0;i<count;++i){Vec3 a{random(),random(),random()*.2f};if(kind==5)a={0,0,0};
  Vec3 b=a+Vec3(1.1f+std::fabs(random())*.02f,0,0),c=a+Vec3(0,1.1f+std::fabs(random())*.02f,.2f);
  unsigned base=s.vertices.size();for(auto p:{a,b,c})s.vertices.push_back({p,{0,0,1},kind==7?.25f:.27f,kind==7?.5f:.37f});s.triangles.push_back({base,base+1,base+2,i%3});
  if(i%401==0)s.triangles.push_back({base,base,base,0});
 }return s;
}
static std::vector<std::string> tiles(const char* client,Vec3 center){std::vector<std::string> out;
 int x=int(std::floor((17066.6666667-center.y)/533.3333333)),y=int(std::floor((17066.6666667-center.x)/533.3333333));
 for(int j=y-1;j<=y+1;++j)for(int i=x-1;i<=x+1;++i)out.push_back(std::string(client)+"/world-cache/Northrend/"+std::to_string(i)+"_"+std::to_string(j)+".fg3");return out;
}
static WorldScene real(const char* client){WorldScene s;std::string error;Vec3 center{1328.78f,-3192.43f,167.61f};
 require(loadInstancedScenes(tiles(client,center),std::string(client)+"/world-cache/models",center-Vec3(288,288,320),center+Vec3(288,288,320),s,error),error.c_str());return s;}
static void productionCache(const char* client){LocalSceneCache cache(LocalSceneCache::Limits{16u*1024u*1024u});SceneSourceCache sources(8u*1024u*1024u);std::string error;Vec3 center{1328.78f,-3192.43f,167.61f};
 for(unsigned turn=0;turn<3;++turn){Vec3 p=center+Vec3(turn==2?8.f:0.f,0,0);BVH b;auto begin=Clock::now();auto cycles=processCycles();double before=processMs();
  require(cache.build(tiles(client,p),std::string(client)+"/world-cache/models","Northrend",p-Vec3(288,288,320),p+Vec3(288,288,320),b,error,{},true,sources.provider(),[&](const std::string& path){return sources.findIdentity(path);}),error.c_str());
  Metric m;m.wall=elapsed(begin);m.cpu=processMs()-before;m.cycles=processCycles()-cycles;metric(turn==0?"cpu_region_cold":turn==1?"cpu_region_same":"cpu_region_adjacent",m,turn,0,b.triangleCount());auto s=cache.stats();
  std::printf("{\"cache_turn\":%u,\"bvh_ms\":%.6f,\"piece_ms\":%.6f,\"assemble_ms\":%.6f,\"blas_builds\":%llu,\"blas_reuses\":%llu,\"fallback_triangles\":%llu,\"tlas_bytes\":%llu}\n",turn,s.bvhMs,s.pieceMs,s.assembleMs,(unsigned long long)s.blasBuilds,(unsigned long long)s.blasReuses,(unsigned long long)s.fallbackTriangles,(unsigned long long)s.tlasBytes);
 }}
static bool close(float a,float b,float abs=.0001f,float rel=.00003f){return std::isfinite(a)&&std::isfinite(b)&&std::fabs(a-b)<=abs+rel*std::max(std::fabs(a),std::fabs(b));}
static bool close(Vec3 a,Vec3 b,float eps=.001f){return close(a.x,b.x,eps)&&close(a.y,b.y,eps)&&close(a.z,b.z,eps);}
static void validateTree(Tree& tree,const WorldScene& s){auto& g=tree.g;auto keys=g.read<std::array<uint32_t,2>>(tree.keys,tree.padded);auto boxes=g.read<Box>(tree.nodes,g.p.leaves*2);
 std::vector<bool> seen(s.triangles.size());for(unsigned i=0;i<keys.size();++i){if(i)require(keys[i-1]<=keys[i],"GPU Morton sort failed");if(i<s.triangles.size()){require(keys[i][1]<seen.size()&&!seen[keys[i][1]],"GPU order is not a permutation");seen[keys[i][1]]=true;}else require(keys[i][0]==UINT32_MAX&&keys[i][1]==UINT32_MAX,"Padding key corruption");}
 auto contains=[](Box b,Vec3 p){return p.x>=b.lo.x&&p.y>=b.lo.y&&p.z>=b.lo.z&&p.x<=b.hi.x&&p.y<=b.hi.y&&p.z<=b.hi.z;};
 for(unsigned i=0;i<s.triangles.size();++i){auto t=s.triangles[keys[i][1]];auto b=boxes[g.p.leaves-(g.p.unused?1:0)+i/g.p.leafSize];require(contains(b,s.vertices[t.v0].position)&&contains(b,s.vertices[t.v1].position)&&contains(b,s.vertices[t.v2].position),"GPU leaf bounds exclude geometry");}
 auto links=g.read<std::array<unsigned,4>>(tree.links,g.p.leaves*2);std::vector<bool> visited(g.p.leaves*2);std::vector<unsigned> todo{g.p.unused?0u:1u};
 while(!todo.empty()){unsigned i=todo.back();todo.pop_back();require(i<visited.size()&&!visited[i],"GPU topology cycle/alias");visited[i]=true;
  if(i>=g.p.leaves-(g.p.unused?1:0))continue;
  for(unsigned j=0;j<2;++j){unsigned child=g.p.unused?links[i][j]:i*2+j;require(child<visited.size(),"GPU child outside tree");Box b=boxes[child];if(b.lo.x<=b.hi.x)require(contains(boxes[i],b.lo)&&contains(boxes[i],b.hi),"GPU internal bounds exclude child");todo.push_back(child);}}
 require(std::count(visited.begin(),visited.end(),true)==2*g.p.leaves-1,"GPU topology missing nodes");
}
static std::vector<Ray> makeRays(const WorldScene& s,unsigned count){std::vector<Ray> rays;unsigned rng=197;
 for(unsigned i=0;i<count;++i){rng=rng*1664525+1013904223;Vec3 at=s.vertices.empty()?Vec3{}:s.vertices[rng%s.vertices.size()].position;
  Vec3 direction=normalized(Vec3{float(int(rng&255)-128)*.002f,float(int((rng>>8)&255)-128)*.002f,-1});
  if(i%4==0)direction={0,0,-1};if(i%11==0)direction={1,0,0};rays.push_back({at+Vec3(.2f,.2f,2),.001f,direction,256});
 }return rays;
}
static void validateRays(const std::vector<Ray>& rays,const std::vector<Hit>& hits,const BVH& bvh,bool any){unsigned mismatch=0,idOnly=0;
 for(unsigned i=0;i<rays.size();++i){const auto& r=rays[i];const auto& got=hits[i];require(!got.error,"GPU traversal stack overflow");
  if(any){if((got.triangle!=UINT32_MAX)!=bvh.occluded(r.o,r.d,r.maxT,r.minT))++mismatch;continue;}
  auto h=bvh.trace(r.o,r.d,r.minT,r.maxT);bool ok=(got.triangle!=UINT32_MAX)==h.hit;
  if(h.hit)ok=ok&&close(got.distance,h.distance)&&close(got.normal,h.normal)&&close(got.geometric,h.geometricNormal)&&close(got.albedo,h.albedo)&&got.back==h.backFace;
  if(ok&&h.hit&&got.triangle!=h.triangle)++idOnly;
  if(!ok&&mismatch++<4){std::fprintf(stderr,"Ray %u gpu=%u t=%.8f cpu=%u t=%.8f\n",i,got.triangle,got.distance,h.triangle,h.distance);
   std::fprintf(stderr," RGB gpu=(%.8f %.8f %.8f) cpu=(%.8f %.8f %.8f); N gpu=(%.8f %.8f %.8f) cpu=(%.8f %.8f %.8f); back=%u/%u\n",got.albedo.x,got.albedo.y,got.albedo.z,h.albedo.x,h.albedo.y,h.albedo.z,got.normal.x,got.normal.y,got.normal.z,h.normal.x,h.normal.y,h.normal.z,got.back,unsigned(h.backFace));}
 }std::printf("{\"ray_check\":\"%s\",\"rays\":%zu,\"semantic_mismatches\":%u,\"equivalent_id_differences\":%u}\n",any?"any":"closest",rays.size(),mismatch,idOnly);parity(!mismatch,"GPU/production hit/attribute mismatch");
}
static void validateProbes(const std::vector<Probe>& cpu,const std::vector<ProbeOut>& gpu){unsigned different=0;float shError=0,distError=0;
 for(unsigned i=0;i<cpu.size();++i){const auto& a=cpu[i];const auto& b=gpu[i];require(!b.error,"GPU solve stack overflow");
  bool ok=a.valid==bool(b.valid)&&a.samples==b.samples&&a.backFaceSamples==b.back;
  for(unsigned j=0;j<4;++j){Vec3 got{b.sh[j][0],b.sh[j][1],b.sh[j][2]};Vec3 e=got-a.sh[j];shError=std::max(shError,std::max(std::fabs(e.x),std::max(std::fabs(e.y),std::fabs(e.z))));ok&=close(got,a.sh[j],.002f);}
  for(unsigned j=0;j<6;++j){distError=std::max(distError,std::fabs(a.moments[j].mean-b.moments[j].mean));ok&=close(a.moments[j].mean,b.moments[j].mean,.01f,.001f)&&close(a.moments[j].meanSquare,b.moments[j].meanSquare,.1f,.002f);}
  if(!ok)++different;
 }std::printf("{\"probe_check\":true,\"probes\":%zu,\"different\":%u,\"max_sh_abs\":%.8f,\"max_mean_abs\":%.8f}\n",cpu.size(),different,shError,distError);parity(!different,"GPU/production GI parity gate failed");
}
static Probe unpackProbe(const ProbeIn& input,const ProbeOut& value){Probe p;p.position=input.p;p.valid=value.valid;p.samples=value.samples;p.backFaceSamples=value.back;p.maxDistance=256;
 for(unsigned i=0;i<4;++i)p.sh[i]={value.sh[i][0],value.sh[i][1],value.sh[i][2]};for(unsigned i=0;i<6;++i)p.moments[i]=value.moments[i];return p;}
static void exportProbes(const std::string& dir,unsigned kind,const std::vector<ProbeIn>& inputs,const std::vector<Probe>& cpu,const std::vector<ProbeOut>& gpu){
 std::ofstream f(dir+"/probe-comparison-"+std::to_string(kind)+".json");f.precision(9);f<<"{\"scene\":"<<kind<<",\"scope\":\"512 probe outputs; not a rendered gameplay frame\",\"probes\":[";
 for(unsigned i=0;i<cpu.size();++i){if(i)f<<',';auto a=cpu[i],b=unpackProbe(inputs[i],gpu[i]);Vec3 x=evaluateIrradiance(a,{0,0,1}),y=evaluateIrradiance(b,{0,0,1});
  float worst=0;for(Vec3 axis:{Vec3{1,0,0},Vec3{-1,0,0},Vec3{0,1,0},Vec3{0,-1,0},Vec3{0,0,1},Vec3{0,0,-1}})for(float distance:{2.f,8.f,32.f,64.f,128.f,256.f})worst=std::max(worst,std::fabs(probeVisibility(a,axis,distance)-probeVisibility(b,axis,distance)));
  f<<"{\"cpu_rgb\":["<<x.x<<','<<x.y<<','<<x.z<<"],\"gpu_rgb\":["<<y.x<<','<<y.y<<','<<y.z<<"],\"cpu_valid\":"<<a.valid<<",\"gpu_valid\":"<<b.valid<<",\"max_visibility_error\":"<<worst<<'}';
 }f<<"]}\n";require(bool(f),"Cannot write probe comparison");
}
static void exportSurface(Tree& tree,const BVH& cpu,const std::string& dir,unsigned kind){auto& g=tree.g;tree.build(2);auto candidates=makeRays(cpu.scene(),8192);
 g.upload(tree.rays,candidates.data(),candidates.size()*sizeof(Ray));g.p.count=candidates.size();g.p.anyHit=0;g.timed([&]{g.dispatch(GPU::Trace,candidates.size(),64);});auto initial=g.read<Hit>(tree.hits,candidates.size());
 Vec3 at=cpu.scene().vertices.empty()?Vec3{}:cpu.scene().vertices[0].position,direction{0,0,-1};
 for(unsigned i=0;i<candidates.size();++i){auto h=cpu.trace(candidates[i].o,candidates[i].d,candidates[i].minT,candidates[i].maxT);if(h.triangle!=initial[i].triangle){at=h.hit?h.position:candidates[i].o+candidates[i].d*initial[i].distance;direction=candidates[i].d;break;}}
 const unsigned width=256,height=128;float span=kind==7?4.f:24.f;std::vector<Ray> view;
 for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x)view.push_back({at+Vec3((float(x)+.5f-width*.5f)*span/width,(float(y)+.5f-height*.5f)*span/width,0)-direction*12,.001f,direction,256});
 g.upload(tree.rays,view.data(),view.size()*sizeof(Ray));g.p.count=view.size();g.timed([&]{g.dispatch(GPU::Trace,view.size(),64);});auto hits=g.read<Hit>(tree.hits,view.size());
 std::vector<unsigned char> colors[2];std::vector<float> linear[2];unsigned idDifference=0,coverageDifference=0;
 auto shade=[](bool hit,Vec3 color,Vec3 normal){return hit?color*(.15f+.85f*std::max(0.f,dot(normal,normalized(Vec3(.35f,-.4f,.85f))))):Vec3(.08f,.12f,.2f);};
 for(unsigned i=0;i<view.size();++i){auto r=view[i];auto a=cpu.trace(r.o,r.d,r.minT,r.maxT);auto b=hits[i];require(!b.error,"Preview traversal overflow");idDifference+=a.triangle!=b.triangle;coverageDifference+=a.hit!=(b.triangle!=UINT32_MAX);
  Vec3 rgb[2]={shade(a.hit,a.albedo,a.normal),shade(b.triangle!=UINT32_MAX,b.albedo,b.normal)};
  for(unsigned side=0;side<2;++side)for(float x:{rgb[side].x,rgb[side].y,rgb[side].z}){linear[side].push_back(x);colors[side].push_back(uint8_t(std::round(std::pow(std::clamp(x,0.f,1.f),1.f/2.2f)*255)));}
 }
 for(unsigned side=0;side<2;++side){std::string stem=dir+"/surface-"+std::to_string(kind)+(side?"-gpu":"-cpu");std::ofstream f(stem+".ppm",std::ios::binary);f<<"P6\n"<<width<<' '<<height<<"\n255\n";f.write((const char*)colors[side].data(),colors[side].size());require(bool(f),"Cannot write surface preview");std::ofstream raw(stem+".f32",std::ios::binary);raw.write((const char*)linear[side].data(),linear[side].size()*4);require(bool(raw),"Cannot write surface floats");}
 std::ofstream meta(dir+"/surface-"+std::to_string(kind)+".json");meta<<"{\"width\":"<<width<<",\"height\":"<<height<<",\"triangle_ids_differ\":"<<idDifference<<",\"coverage_differs\":"<<coverageDifference<<",\"scope\":\"Orthographic actual geometry/material preview with identical host Lambert shading, not full GI gameplay rendering\"}\n";
}
int main(int argc,char** argv){try{
 require(argc>=5,"Usage: gpu-bvh shader-dir scene-kind rounds client-root [--measure-rejected|--negative-bounds]");measureRejected=argc==6&&std::string(argv[5])=="--measure-rejected";negativeBounds=argc==6&&std::string(argv[5])=="--negative-bounds";auto setupStart=Clock::now();auto setupCycles=processCycles();GPU gpu(argv[1]);Metric setup;setup.wall=elapsed(setupStart);setup.cycles=processCycles()-setupCycles;metric("gpu_device_setup",setup,-1,0,0);unsigned kind=std::strtoul(argv[2],nullptr,10),rounds=std::strtoul(argv[3],nullptr,10);require(rounds>0&&rounds<=20,"Bad rounds");
 auto source=kind==6?real(argv[4]):synthetic(kind);auto begin=Clock::now();double before=processMs();uint64_t cyclesBefore=processCycles();Packed packed(source);Metric packing;packing.wall=elapsed(begin);packing.cpu=processMs()-before;packing.cycles=processCycles()-cyclesBefore;metric("pack",packing,-1,0,source.triangles.size());sceneIdentity(packed);
 std::printf("{\"scene\":%u,\"triangles\":%zu,\"vertices\":%zu,\"material_bytes\":%zu}\n",kind,source.triangles.size(),source.vertices.size(),packed.texels.size()*4);
 begin=Clock::now();Tree tree(gpu,packed);Metric allocation;allocation.wall=elapsed(begin);metric("gpu_allocate",allocation,-1,0,0);
 metric("upload",gpu.timed([&]{tree.upload(packed);}),-1,0,0);
 BVH cpu;std::string error;for(unsigned round=0;round<rounds;++round){WorldScene scene=source;begin=Clock::now();before=processMs();cyclesBefore=processCycles();require(cpu.build(std::move(scene),error),error.c_str());Metric m;m.wall=elapsed(begin);m.cpu=processMs()-before;m.cycles=processCycles()-cyclesBefore;metric("cpu_sah_build",m,round,0,source.triangles.size());
  for(unsigned leaf:{2u,4u,8u})for(bool radix:{false,true}){metric(radix?"gpu_lbvh_build":"gpu_morton_build",gpu.timed([&]{tree.build(leaf,radix);}),round,leaf,source.triangles.size());
   if(negativeBounds){std::vector<Box> bad(tree.maxLeaves*2);gpu.upload(tree.nodes,bad.data());}
   validateTree(tree,source);metric(radix?"gpu_lbvh_refit":"gpu_morton_refit",gpu.timed([&]{tree.refit();}),round,leaf,source.triangles.size());validateTree(tree,source);}
 }
 auto rays=makeRays(source,8192);gpu.upload(tree.rays,rays.data(),rays.size()*sizeof(Ray));gpu.p.count=rays.size();
 for(unsigned leaf:{2u,4u,8u}){gpu.timed([&]{tree.build(leaf);});
  for(unsigned any:{0u,1u}){gpu.p.anyHit=any;
   for(unsigned round=0;round<rounds;++round){begin=Clock::now();before=processMs();cyclesBefore=processCycles();uint64_t checksum=0;for(const auto& r:rays){if(any)checksum+=cpu.occluded(r.o,r.d,r.maxT,r.minT);else checksum+=cpu.trace(r.o,r.d,r.minT,r.maxT).triangle;}
    Metric m;m.wall=elapsed(begin);m.cpu=processMs()-before;m.cycles=processCycles()-cyclesBefore;metric(any?"cpu_any":"cpu_closest",m,round,leaf,rays.size());require(checksum!=UINT64_MAX,"Trace checksum");gpu.p.count=rays.size();
    metric(any?"gpu_any":"gpu_closest",gpu.timed([&]{gpu.dispatch(GPU::Trace,rays.size(),64);}),round,leaf,rays.size());}
   validateRays(rays,gpu.read<Hit>(tree.hits,rays.size()),cpu,any);
  }
 }
 Lighting lighting;lighting.maxBounces=3;auto prepared=prepareLighting(lighting);auto sun=prepared.values().sunDirection;
 gpu.p.sun[0]=sun.x;gpu.p.sun[1]=sun.y;gpu.p.sun[2]=sun.z;gpu.p.sun[3]=lighting.rayBias;gpu.p.up[2]=1;gpu.p.up[3]=lighting.maxDistance;
 gpu.p.sunRGB[0]=lighting.sunIrradiance.x;gpu.p.sunRGB[1]=lighting.sunIrradiance.y;gpu.p.sunRGB[2]=lighting.sunIrradiance.z;gpu.p.sky[0]=lighting.skyRadiance.x;gpu.p.sky[1]=lighting.skyRadiance.y;gpu.p.sky[2]=lighting.skyRadiance.z;
 SolvePool pool(3,[]{});
 for(unsigned leaf:{2u,4u,8u}){gpu.timed([&]{tree.build(leaf);});for(unsigned count:{8u,64u,512u}){
  std::vector<ProbeIn> probes;for(unsigned i=0;i<count;++i){auto p=source.vertices.empty()?Vec3{}:source.vertices[(size_t(i)*7919)%source.vertices.size()].position;probes.push_back({p+Vec3(.3f,.3f,2.1f),i+1});}
  std::vector<Probe> cpuProbes(count);gpu.p.count=count;gpu.p.rayCount=32;
  for(unsigned round=0;round<rounds;++round){begin=Clock::now();before=processMs();cyclesBefore=processCycles();for(unsigned i=0;i<count;++i)cpuProbes[i]=solveProbePrepared(cpu,probes[i].p,prepared,32,probes[i].seed);Metric m;m.wall=elapsed(begin);m.cpu=processMs()-before;m.cycles=processCycles()-cyclesBefore;metric("cpu_gi_serial",m,round,leaf,count);
   begin=Clock::now();before=processMs();cyclesBefore=processCycles();pool.run(count,[&](size_t i){cpuProbes[i]=solveProbePrepared(cpu,probes[i].p,prepared,32,probes[i].seed);});m.wall=elapsed(begin);m.cpu=processMs()-before;m.cycles=processCycles()-cyclesBefore;metric("cpu_gi_4threads",m,round,leaf,count);
   for(unsigned kernel:{GPU::Solve,GPU::Parallel}){
    auto dispatch=[&]{gpu.upload(tree.probes,probes.data(),probes.size()*sizeof(ProbeIn));gpu.dispatch(kernel,kernel==GPU::Solve?count:count*32,kernel==GPU::Solve?64:32);};
    if(!round)for(unsigned warm=0;warm<2;++warm)gpu.timed(dispatch);
    Metric solve=gpu.timed(dispatch);metric(kernel==GPU::Solve?"gpu_gi_serial_rays":"gpu_gi_parallel_rays",solve,round,leaf,count);
    begin=Clock::now();before=processMs();cyclesBefore=processCycles();auto result=gpu.read<ProbeOut>(tree.outputs,count);Metric read;read.wall=elapsed(begin);read.cpu=processMs()-before;read.cycles=processCycles()-cyclesBefore;metric(kernel==GPU::Solve?"gpu_readback_serial":"gpu_readback_parallel",read,round,leaf,count);validateProbes(cpuProbes,result);
    if(kernel==GPU::Parallel&&leaf==2&&count==512&&round==rounds-1)exportProbes(argv[1],kind,probes,cpuProbes,result);
   }
  }
 }}
 if(kind==3){WorldScene moved=source;for(auto& v:moved.vertices)v.position=v.position+Vec3(.125f,-.25f,.75f);Packed changed(moved);
  gpu.upload(tree.v,changed.vertices.data());gpu.timed([&]{tree.refit();});validateTree(tree,moved);BVH updated;
  require(updated.build(std::move(moved),error),error.c_str());auto changedRays=makeRays(updated.scene(),8192);gpu.upload(tree.rays,changedRays.data(),changedRays.size()*sizeof(Ray));gpu.p.count=changedRays.size();
  for(unsigned any:{0u,1u}){gpu.p.anyHit=any;gpu.timed([&]{gpu.dispatch(GPU::Trace,changedRays.size(),64);});validateRays(changedRays,gpu.read<Hit>(tree.hits,changedRays.size()),updated,any);}
  std::puts("{\"moved_geometry_refit\":true}");}
 if(kind==6||kind==7)exportSurface(tree,cpu,argv[1],kind);
 if(kind==6)productionCache(argv[4]);
 std::printf("{\"gpu_logical_bytes\":%llu,\"staging_peak_bytes\":%llu,\"scope\":\"isolated hardware experiment, not gameplay FPS; sun/sky only\"}\n",(unsigned long long)gpu.deviceBytes,(unsigned long long)gpu.stagingBytes);
 std::printf("{\"parity_failures\":%u,\"production_eligible\":false}\n",parityFailures);
 std::puts(parityFailures?"GPU_BVH_REJECTED":"GPU_BVH_PASS");return parityFailures?2:0;
 }catch(const std::exception& e){std::fprintf(stderr,"GPU_BVH_FAIL: %s\n",e.what());return 1;}}
