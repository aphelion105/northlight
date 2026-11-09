#define NOMINMAX
#include <d3d9.h>
#include <cassert>
#include <cstdio>
#include <chrono>
#include <memory>
#include <cstdlib>
#include <new>
static bool counting=false;static size_t allocationCalls=0,allocationBytes=0;
void* operator new(size_t n){if(counting){++allocationCalls;allocationBytes+=n;}if(void* p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void operator delete(void* p)noexcept{std::free(p);}void operator delete(void* p,size_t)noexcept{std::free(p);}
struct EnvelopeFixedClock {using duration=std::chrono::nanoseconds;using time_point=std::chrono::time_point<EnvelopeFixedClock,duration>;static time_point now(){return time_point(duration(1000000));}};
#define NORTHLIGHT_REPLAY_BOUNDS_CLOCK EnvelopeFixedClock
#include "replay_bounds.h"
#ifdef NORTHLIGHT_SKIN_REFERENCE
namespace NorthlightSkinReference {using namespace NorthlightReplayBounds;}
#define NorthlightReplayBounds NorthlightSkinReference
#include NORTHLIGHT_SKIN_REFERENCE
#undef NorthlightReplayBounds
#endif
using Mesh=NorthlightDrawSnapshot::Mesh;using Program=NorthlightActorDeformation::Program;
static volatile float sink=0;
static Program load(const char* path){FILE* f=std::fopen(path,"rb");assert(f);std::fseek(f,0,SEEK_END);auto size=std::ftell(f);std::rewind(f);std::vector<uint32_t> code(size/4);assert(std::fread(code.data(),1,size,f)==size_t(size));std::fclose(f);Program p;assert(NorthlightActorDeformation::compile(code.data(),code.size(),p));return p;}
static const D3DVERTEXELEMENT9 layout[]={{0,0,D3DDECLTYPE_FLOAT3,0,D3DDECLUSAGE_POSITION,0},{0,12,D3DDECLTYPE_FLOAT4,0,D3DDECLUSAGE_BLENDWEIGHT,0},{0,28,D3DDECLTYPE_UBYTE4,0,D3DDECLUSAGE_BLENDINDICES,0},D3DDECL_END()};
struct Result {size_t calls=0,bytes=0,charged=0;float lo[3]={},hi[3]={};double ms=0;};
template<class Cache,class Bounds,class Budget>
static Result trial(const Program& p,bool audit){
 auto prepared=Cache::prepareProgram(p,layout,4);assert(prepared);Cache cache(true);std::vector<std::shared_ptr<const Mesh>> sources;
 for(unsigned i=0;i<512;++i){auto m=std::make_shared<Mesh>();m->vertexCount=6;m->primitiveCount=2;m->streams[0].stride=32;m->streams[0].bytes.resize(192);
  for(unsigned v=0;v<6;++v){const float data[]={float(v%3),float(v/3),float(i)*.01f,.4f,.3f,.2f,.1f};std::memcpy(m->streams[0].bytes.data()+v*32,data,28);for(unsigned k=0;k<4;++k)m->streams[0].bytes[v*32+28+k]=k;}sources.push_back(m);}
 float bank[1024]={},inverse[16]={};for(unsigned i=0;i<4;++i)inverse[5*i]=1;
 for(unsigned bone=0;bone<75;++bone){auto* r=bank+4*(31+3*bone);r[0]=r[5]=r[10]=1;r[3]=bone*.1f;r[11]=10;}
 Bounds b;Result result;allocationCalls=allocationBytes=0;counting=true;
 for(auto& source:sources){bool ready=false;for(unsigned attempt=0;attempt<16&&!ready;++attempt){cache.beginFrame(false);Budget budget;ready=int(cache.calculatePrepared(*prepared,*source,source,bank,inverse,budget,b))==0;}assert(ready&&b.valid);}
 counting=false;result.calls=allocationCalls;result.bytes=allocationBytes;result.charged=cache.reservedBytes();
 assert(cache.entries()==sources.size());

 const auto start=std::chrono::steady_clock::now();
 for(unsigned frame=0;frame<40;++frame){cache.beginFrame(false);Budget budget;budget.maxOperations=1u<<30;
  for(auto& source:sources){bank[127]=float(frame)*.01f;const auto status=cache.calculatePrepared(*prepared,*source,source,bank,inverse,budget,b);assert(int(status)==0&&b.valid);sink=b.low[0];}}
 result.ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()/40;
 std::copy_n(b.low,3,result.lo);std::copy_n(b.high,3,result.hi);
 // Keep the same prepared-key identity in both timed runs; lifetime tests
 // deliberately replace it with an independent key and belong afterwards.
 if constexpr(std::is_same_v<Cache,NorthlightReplayBounds::EnvelopeCache>)if(audit){
  cache.beginFrame(false);Budget budget;assert(int(cache.calculatePrepared(*prepared,*sources[0],sources[0],bank,inverse,budget,b))==0);
  assert(cache.stats().sharedKeyHits==1&&!cache.stats().keyComparedBytes);
  auto independent=Cache::prepareProgram(p,layout,4);assert(independent);prepared.reset();
  cache.beginFrame(false);budget.reset();assert(int(cache.calculatePrepared(*independent,*sources[0],sources[0],bank,inverse,budget,b))==0);
  assert(!cache.stats().sharedKeyHits&&cache.stats().keyComparedBytes>0&&cache.entries()==sources.size());
  // Shared layout metadata never substitutes for this mesh's byte/stride proof.
  for(unsigned invalid=0;invalid<3;++invalid){auto mesh=std::make_shared<Mesh>(*sources[0]);
   if(invalid==0)mesh->streams[0].bytes.pop_back();
   if(invalid==1)--mesh->streams[0].stride;
   if(invalid==2)mesh->streams[0].bytes.clear();
   std::shared_ptr<const Mesh> owner=mesh;cache.beginFrame(false);budget.reset();
   assert(cache.calculatePrepared(*independent,*owner,owner,bank,inverse,budget,b)==NorthlightReplayBounds::Status::Invalid&&!b.valid);
  }
  const D3DVERTEXELEMENT9 duplicate[]={layout[0],layout[0],layout[1],layout[2],layout[3]};
  assert(!Cache::prepareProgram(p,duplicate,5));
  auto changed=p;changed.definitions.push_back({250,{1,2,3,4}});auto other=Cache::prepareProgram(changed,layout,4);assert(other);
  cache.beginFrame(false);budget.reset();assert(int(cache.calculatePrepared(*other,*sources[0],sources[0],bank,inverse,budget,b,Cache::Stage::Ready))==2&&!b.valid);
  assert(cache.entries()==sources.size());prepared=std::move(independent);
 }
 cache.clear();assert(!cache.entries()&&!cache.reservedBytes());return result;
}
int main(int argc,char** argv){assert(argc==2);auto program=load(argv[1]);
 auto current=trial<NorthlightReplayBounds::EnvelopeCache,NorthlightReplayBounds::Bounds,NorthlightReplayBounds::Budget>(program,true);
#ifdef NORTHLIGHT_SKIN_REFERENCE
 auto before=trial<NorthlightSkinReference::EnvelopeCache,NorthlightSkinReference::Bounds,NorthlightSkinReference::Budget>(program,false);
 assert(!std::memcmp(current.lo,before.lo,sizeof current.lo)&&!std::memcmp(current.hi,before.hi,sizeof current.hi));
 assert(before.calls>=current.calls+500&&before.bytes>current.bytes);
 std::printf("ENVELOPE_SHARED_KEY_COMPARE_PASS sources=512 allocations=%zu/%zu allocatedBytes=%zu/%zu chargedBytes=%zu/%zu referenceMs=%.6f currentMs=%.6f bounds=bitexact\n",before.calls,current.calls,before.bytes,current.bytes,before.charged,current.charged,before.ms,current.ms);
#endif
 std::puts("ENVELOPE_SHARED_KEY_PASS retained_key shared_pointer exact_fallback changed_program clear metadata_lifetime");return 0;
}
