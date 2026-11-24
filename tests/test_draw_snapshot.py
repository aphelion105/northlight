#!/usr/bin/env python3
# northlight-test: requires=cxx
"""Exercise real snapshot code against mutable D3D interfaces, no game/device."""
import sys; from pathlib import Path; sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # repo root
import northlight_paths as fp
from pathlib import Path
import ast,subprocess,tempfile
HERE=Path(__file__).resolve().parent
stub=next(ast.literal_eval(n.value) for n in ast.parse((HERE/'test_terrain_snapshot.py').read_text()).body if isinstance(n,ast.Assign) and any(isinstance(t,ast.Name) and t.id=='stub' for t in n.targets))
harness=r'''
#include "draw_snapshot.h"
#include <cassert>
#include <cstdio>
using namespace NorthlightDrawSnapshot;
template<class Interface,class Desc>struct Buffer:Interface{
 unsigned refs=1,locks=0,unlocks=0;bool failLock=false,failUnlock=false;Desc desc;std::vector<std::uint8_t> bytes;
 unsigned AddRef()override{return ++refs;}unsigned Release()override{return --refs;}
 HRESULT GetDesc(Desc*out)override{*out=desc;out->Size=UINT(bytes.size());return 0;}
 HRESULT Lock(UINT offset,UINT size,void**out,DWORD flags)override{assert(flags==D3DLOCK_READONLY);if(failLock)return D3DERR_INVALIDCALL;assert(std::uint64_t(offset)+size<=bytes.size());++locks;*out=bytes.data()+offset;return 0;}
 HRESULT Unlock()override{++unlocks;return failUnlock?D3DERR_INVALIDCALL:0;}
};
struct Declaration:IDirect3DVertexDeclaration9{
 unsigned refs=1;std::vector<D3DVERTEXELEMENT9> e={{0,4,2,0,0,0},{1,0,4,0,1,0},{0xff,0,17,0,0,0}};
 unsigned AddRef()override{return ++refs;}unsigned Release()override{return --refs;}
 HRESULT GetDeclaration(D3DVERTEXELEMENT9*out,UINT*n)override{assert(*n>=e.size());*n=UINT(e.size());std::memcpy(out,e.data(),e.size()*sizeof(*out));return 0;}
};
struct Device:IDirect3DDevice9{
 Buffer<IDirect3DVertexBuffer9,D3DVERTEXBUFFER_DESC> vb[4];Buffer<IDirect3DIndexBuffer9,D3DINDEXBUFFER_DESC> ib;Declaration decl;UINT offset[4]={8,4,0,0},stride[4]={24,8,0,0},freq[4]={1,1,1,1};
 Device(){for(unsigned s=0;s<2;++s){vb[s].desc.Usage=D3DUSAGE_DYNAMIC|D3DUSAGE_WRITEONLY;vb[s].bytes.resize(offset[s]+12*stride[s]);for(unsigned i=0;i<vb[s].bytes.size();++i)vb[s].bytes[i]=std::uint8_t(i);}indices({999,7,9,8});}
 void indices(std::initializer_list<std::uint32_t> in,bool wide=false){ib.desc.Format=wide?D3DFMT_INDEX32:D3DFMT_INDEX16;ib.desc.Usage=D3DUSAGE_DYNAMIC;ib.bytes.resize(in.size()*(wide?4:2));unsigned i=0;for(auto v:in){if(wide)std::memcpy(ib.bytes.data()+i*4,&v,4);else{std::uint16_t n=std::uint16_t(v);std::memcpy(ib.bytes.data()+i*2,&n,2);}++i;}}
 HRESULT GetVertexShaderConstantF(UINT,float*,UINT)override{return D3DERR_INVALIDCALL;}
 HRESULT GetVertexDeclaration(IDirect3DVertexDeclaration9**out)override{*out=&decl;decl.AddRef();return 0;}
 HRESULT GetStreamSourceFreq(UINT s,UINT*out)override{*out=freq[s];return 0;}
 HRESULT GetStreamSource(UINT s,IDirect3DVertexBuffer9**out,UINT*o,UINT*st)override{*out=&vb[s];vb[s].AddRef();*o=offset[s];*st=stride[s];return 0;}
 HRESULT GetIndices(IDirect3DIndexBuffer9**out)override{*out=&ib;ib.AddRef();return 0;}
};
int main(){Device d;Frame frame;Mesh a,b;Diagnostics why;Draw draw{D3DPT_TRIANGLELIST,-3,7,3,1,1,true};
 assert(frame.read(&d,&d.decl,draw,a,&why));assert(a.dynamic&&a.vertexCount==3&&a.indices==std::vector<std::uint32_t>({0,2,1}));
 assert(a.streams[0].bytes.size()==72&&a.streams[1].bytes.size()==24&&a.streams[2].bytes.empty());
 assert(a.streams[0].bytes[4]==d.vb[0].bytes[8+4*24+4]);assert(a.streams[1].bytes[0]==d.vb[1].bytes[4+4*8]);
 auto old=a.streams[0].bytes;d.vb[0].bytes[8+4*24+4]^=123;d.indices({999,8,7,9},true);
 assert(frame.read(&d,&d.decl,draw,b,&why));assert(a.streams[0].bytes==old&&b.streams[0].bytes!=old&&b.indices==std::vector<std::uint32_t>({1,0,2}));
 {
  Device bound;bound.decl.e.erase(bound.decl.e.begin()+1);
  static Device* current=nullptr;current=&bound;
  static std::uint64_t vbRevision=1,ibRevision=1;
  auto identity=+[](void* p,bool){return std::uint64_t(reinterpret_cast<std::uintptr_t>(p));};
  Frame resident;resident.setIdentityProvider(identity);resident.setChargeResidentHits(false);
  resident.setVersionProvider(+[](void*,bool index){return index?ibRevision:vbRevision;});
  resident.setMetadataProvider(+[](const NorthlightCaptureMetadata::Request* request,NorthlightCaptureMetadata::Info* out,unsigned count){
   for(unsigned i=0;i<count;++i){out[i]={};const auto& r=request[i];if(!r.buffer)continue;
    const auto key=std::uint64_t(reinterpret_cast<std::uintptr_t>(r.buffer));
    if(r.index&&r.buffer==&current->ib)out[i]={key,ibRevision,UINT(current->ib.bytes.size()),current->ib.desc.Usage,current->ib.desc.Format,true};
    else if(!r.index&&r.buffer==&current->vb[0])out[i]={key,vbRevision,UINT(current->vb[0].bytes.size()),current->vb[0].desc.Usage,{},true};
   }
  });
  const UINT extent[]={16,0,0,0};
  std::shared_ptr<const Mesh> owned,borrowed;Mesh copy;
  auto hit=[&]{return resident.readResidentIndexed(&bound.decl,draw,extent,&bound.vb[0],8,24,&bound.ib,borrowed);};
  assert(!hit()&&resident.read(&bound,&bound.decl,draw,copy,nullptr,false,&owned)&&owned);
  Draw alternate=draw;alternate.vertices=4;
  std::shared_ptr<const Mesh> alternateMesh;
  assert(resident.read(&bound,&bound.decl,alternate,copy,nullptr,false,&alternateMesh)&&alternateMesh!=owned);
  const auto bytes=resident.bytesRead();
  assert(hit()&&borrowed==owned);
  assert(resident.readResidentIndexed(&bound.decl,alternate,extent,&bound.vb[0],8,24,&bound.ib,borrowed)&&borrowed==alternateMesh);
  assert(resident.residentIndexedPlanHits()==1&&resident.bytesRead()==bytes&&bound.vb[0].locks==2&&bound.ib.locks==2);
  ++vbRevision;bound.vb[0].bytes[8+4*24+4]^=123;borrowed.reset();
  assert(!hit()&&!borrowed&&resident.read(&bound,&bound.decl,draw,copy,nullptr,false,&borrowed));
  assert(resident.residentIndexedPlanHits()==1);
  assert(borrowed!=owned&&borrowed->streams[0].bytes!=owned->streams[0].bytes);
  assert(hit()&&bound.vb[0].locks==3&&bound.ib.locks==3);
  ibRevision=0;borrowed.reset();assert(!hit()&&!borrowed);
  ibRevision=2;bound.indices({999,8,7,9},true);
  assert(!hit()&&resident.read(&bound,&bound.decl,draw,copy,nullptr,false,&borrowed)&&hit());
  assert(borrowed->indices==std::vector<std::uint32_t>({1,0,2}));
  resident.clearIndexCache();borrowed.reset();assert(!hit()&&!borrowed);
 }
 // Dynamic storage alone cannot consume the reservation for audited skeletal draws.
 {Device staticDevice;for(auto& vb:staticDevice.vb)vb.desc.Usage=0;staticDevice.ib.desc.Usage=0;Frame reserved(240,120);Mesh copy;
  assert(reserved.read(&staticDevice,&staticDevice.decl,draw,copy));
  assert(!reserved.read(&staticDevice,&staticDevice.decl,draw,copy,&why)&&why.error==Error::Budget);
  staticDevice.vb[0].desc.Usage=D3DUSAGE_DYNAMIC;
  assert(!reserved.read(&staticDevice,&staticDevice.decl,draw,copy,&why)&&why.error==Error::Budget);
  assert(reserved.read(&staticDevice,&staticDevice.decl,draw,copy,nullptr,true));
  assert(copy.dynamic);assert(!reserved.read(&staticDevice,&staticDevice.decl,draw,copy,&why,true)&&why.error==Error::Budget);
  reserved.clearFrame();staticDevice.vb[0].desc.Usage=0;
  assert(reserved.read(&staticDevice,&staticDevice.decl,draw,copy));
  assert(reserved.read(&staticDevice,&staticDevice.decl,draw,copy,nullptr,true)); // Static VB, audited skeletal shader priority.
 }
 {Device forest;for(auto& vb:forest.vb)vb.desc.Usage=0;forest.ib.desc.Usage=0;Frame budget;Mesh copy;
  for(unsigned i=0;i<3072;++i)assert(budget.read(&forest,&forest.decl,draw,copy));
  assert(!budget.read(&forest,&forest.decl,draw,copy,&why)&&why.error==Error::Budget);
  for(unsigned i=0;i<1024;++i)assert(budget.read(&forest,&forest.decl,draw,copy,nullptr,true));
  assert(!budget.read(&forest,&forest.decl,draw,copy,&why,true)&&why.error==Error::Budget);
 }
 // 0.3.172 near reserve (one draw reads 6 index + 84 vertex bytes = 90).
 {Device st;for(auto& vb:st.vb)vb.desc.Usage=0;st.ib.desc.Usage=0;Mesh copy;
  // Reserve 0: a nearby read is byte-identical to a plain one, step by step.
  {Frame plain(300,0),zero(300,0);zero.setNearReserve(0);
   for(unsigned i=0;i<6;++i){Diagnostics x,y;const bool a=plain.read(&st,&st.decl,draw,copy,&x,true),b=zero.read(&st,&st.decl,draw,copy,&y,true,nullptr,true);
    assert(a==b&&x.error==y.error&&plain.bytesRead()==zero.bytesRead()&&plain.captureExhausted(true)==zero.captureExhausted(true,true));}
   assert(plain.bytesRead()==288);} /* 3 draws, then 3 refused reads charge their index bytes, as today */
  // Main budget spent: a non-near draw is refused; near draws use limit+reserve, then are refused too.
  {Frame f(200,0);f.setNearReserve(200);assert(f.nearReserve()==200);
   assert(f.read(&st,&st.decl,draw,copy,nullptr,true)&&f.read(&st,&st.decl,draw,copy,nullptr,true)&&f.bytesRead()==180);
   assert(!f.read(&st,&st.decl,draw,copy,&why,true)&&why.error==Error::Budget&&f.bytesRead()==186); /* index bytes charged, as today */
   assert(f.captureExhausted(true)==false&&!f.captureExhausted(true,true));
   assert(f.read(&st,&st.decl,draw,copy,&why,true,nullptr,true)&&f.bytesRead()==276&&f.captureExhausted(true)&&!f.captureExhausted(true,true));
   assert(!f.read(&st,&st.decl,draw,copy,&why,true)&&why.error==Error::Budget&&f.bytesRead()==276); /* past the main limit: nothing charged */
   assert(f.read(&st,&st.decl,draw,copy,&why,true,nullptr,true)&&f.bytesRead()==366);
   assert(!f.read(&st,&st.decl,draw,copy,&why,true,nullptr,true)&&why.error==Error::Budget&&f.bytesRead()==372&&f.captureExhausted(true,true)==false);
   // Regular (non-priority) draws never use the reserve, flagged or not.
   assert(!f.read(&st,&st.decl,draw,copy,&why,false,nullptr,true)&&why.error==Error::Budget&&f.captureExhausted(false,true));
   f.clearFrame();assert(f.bytesRead()==0&&f.nearReserve()==200);}
  // UP draws: the same contract.
  {Frame f(90,0);f.setNearReserve(90);std::vector<std::uint8_t> up(11*24,1);std::uint16_t ii[]={7,8,9};Draw upDraw{D3DPT_TRIANGLELIST,0,7,3,0,1,true};
   Declaration one;one.e={{0,4,2,0,0,0},{0xff,0,17,0,0,0}};
   assert(f.readUP(&one,upDraw,ii,D3DFMT_INDEX16,up.data(),24,copy,nullptr,true));const size_t each=f.bytesRead();
   assert(!f.readUP(&one,upDraw,ii,D3DFMT_INDEX16,up.data(),24,copy,&why,true)&&why.error==Error::Budget);
   assert(f.readUP(&one,upDraw,ii,D3DFMT_INDEX16,up.data(),24,copy,&why,true,true)&&f.bytesRead()>each);}
  // The draw-count cap is unchanged: 4096 in total (3072 regular) whatever the reserve.
  {Frame f;f.setNearReserve(4u<<20);
   for(unsigned i=0;i<3072;++i)assert(f.read(&st,&st.decl,draw,copy));
   for(unsigned i=0;i<1024;++i)assert(f.read(&st,&st.decl,draw,copy,nullptr,true,nullptr,i%2==0));
   assert(f.countExhausted(true)&&f.captureExhausted(true,true)&&!f.read(&st,&st.decl,draw,copy,&why,true,nullptr,true)&&why.error==Error::Budget);}
  // A cache hit through the reserve is charged exactly like the miss it replays.
  {Frame f(200,0);f.setNearReserve(200);f.setIdentityProvider([](void* b,bool){return std::uint64_t(reinterpret_cast<std::uintptr_t>(b));});
   std::shared_ptr<const Mesh> shared;size_t charged[2]={};
   for(unsigned pass=0;pass<2;++pass){f.clearFrame();
    assert(f.read(&st,&st.decl,draw,copy,nullptr,true)&&f.read(&st,&st.decl,draw,copy,nullptr,true)&&f.captureExhausted(true)==false);
    Draw other=draw;other.start=0;other.minimum=0;other.vertices=11;other.base=0;st.indices({7,8,9,999},false);st.ib.desc.Usage=0; /* indices() marks the IB DYNAMIC */
    const size_t before=f.bytesRead();assert(f.read(&st,&st.decl,other,copy,&why,true,&shared,true)&&shared);charged[pass]=f.bytesRead()-before;st.indices({999,7,9,8});st.ib.desc.Usage=0;
    assert(shared->hasCheckedIndices()&&!copy.hasCheckedIndices());
    Mesh editable=*shared;editable.indices[0]=editable.vertexCount;assert(!editable.hasCheckedIndices());
    editable=*shared;assert(!editable.hasCheckedIndices());
    assert(f.snapshotCacheHits()==pass);}
   assert(charged[0]==charged[1]&&charged[0]>0&&f.bytesRead()>200);}
 }
 // Sparse indices compact every declared stream with stable ascending source
 // vertices, preserving repeated indices, winding and blend/color attributes.
 {Device sparse;sparse.indices({999,7,10,7});Frame compact;Mesh copy;Draw sparseDraw{D3DPT_TRIANGLELIST,-3,7,4,1,1,true};
  assert(compact.read(&sparse,&sparse.decl,sparseDraw,copy));assert(copy.vertexCount==2&&copy.indices==std::vector<std::uint32_t>({0,1,0}));
  for(unsigned stream=0;stream<2;++stream){assert(copy.streams[stream].bytes.size()==2*sparse.stride[stream]);for(unsigned vertex=0;vertex<2;++vertex){unsigned source=vertex?7:4;unsigned extent=stream?4:16;for(unsigned byte=0;byte<extent;++byte)assert(copy.streams[stream].bytes[vertex*sparse.stride[stream]+byte]==sparse.vb[stream].bytes[sparse.offset[stream]+source*sparse.stride[stream]+byte]);}}
  assert(compact.compactedDraws()==1&&compact.uniqueVertices()==2&&compact.sourceSpanVertices()==4&&compact.savedVertexBytes()==64);
  auto previous=copy;sparse.vb[1].bytes[sparse.offset[1]+7*sparse.stride[1]]^=123;assert(compact.read(&sparse,&sparse.decl,sparseDraw,copy));assert(previous.streams[1].bytes!=copy.streams[1].bytes);
  sparse.indices({999,10,7,10,7});sparseDraw.topology=D3DPT_TRIANGLESTRIP;sparseDraw.primitives=2;assert(compact.read(&sparse,&sparse.decl,sparseDraw,copy));assert(copy.indices==std::vector<std::uint32_t>({1,0,1,0}));
 }
 // Insufficient stream-total budget must not lock even the first vertex stream.
 {Device multi;Frame budget(85,0);Mesh copy;unsigned locks=multi.vb[0].locks;assert(!budget.read(&multi,&multi.decl,draw,copy,&why)&&why.error==Error::Budget);assert(multi.vb[0].locks==locks&&multi.vb[1].locks==0&&budget.bytesRead()==6);}
 // A bad later stream range must not copy an otherwise valid earlier stream.
 {Device multi;multi.vb[1].bytes.resize(4);Frame budget;Mesh copy;assert(!budget.read(&multi,&multi.decl,draw,copy,&why)&&why.error==Error::VertexRange);assert(multi.vb[0].locks==0&&multi.vb[1].locks==0&&budget.bytesRead()==6);}
 // Large 32-bit sparse spans do not allocate a range-sized remap or snapshot.
 {Device large;large.offset[0]=large.offset[1]=0;for(unsigned stream=0;stream<2;++stream)large.vb[stream].bytes.resize(size_t(300001)*large.stride[stream]);large.indices({0,300000,0},true);
  Draw wide{D3DPT_TRIANGLELIST,0,0,300001,0,1,true};Frame tiny(80,0);Mesh copy;assert(tiny.read(&large,&large.decl,wide,copy));assert(copy.vertexCount==2&&copy.indices==std::vector<std::uint32_t>({0,1,0})&&tiny.bytesRead()==64);
  large.indices({0,0xffffffff,0},true);wide.vertices=0xffffffff;assert(!tiny.read(&large,&large.decl,wide,copy,&why)&&why.error==Error::IndexRange);
 }
 // Remap cache hits re-read mutable vertex bytes; identical index bytes with
 // changed range/base/stride/declaration contracts must still validate correctly.
 {Device cached;cached.indices({999,7,10,7},true);Draw selected{D3DPT_TRIANGLELIST,-3,7,4,1,1,true};Frame snapshots;Mesh first,next;
  assert(snapshots.read(&cached,&cached.decl,selected,first));auto previous=first;
  cached.vb[0].bytes[8+7*24+4]^=87;assert(snapshots.read(&cached,&cached.decl,selected,next));assert(next.streams[0].bytes!=first.streams[0].bytes&&first.streams[0].bytes==previous.streams[0].bytes);
  selected.vertices=3;assert(!snapshots.read(&cached,&cached.decl,selected,next,&why)&&why.error==Error::IndexRange);selected.vertices=4;
  selected.base=-4;assert(snapshots.read(&cached,&cached.decl,selected,next));assert(next.streams[0].bytes[4]==cached.vb[0].bytes[8+3*24+4]);selected.base=-3;
  cached.stride[0]=20;assert(snapshots.read(&cached,&cached.decl,selected,next));assert(next.streams[0].bytes[4]==cached.vb[0].bytes[8+4*20+4]);
  cached.decl.e[0].Offset=16;assert(!snapshots.read(&cached,&cached.decl,selected,next,&why)&&why.error==Error::Stream);
 }
 // Indexed topology/attribute equivalence against original draw records across
 // random repeated/sparse permutations, both formats and negative base offsets.
 {Device random;Frame snapshots;Mesh copy;unsigned seed=17;
  for(unsigned attempt=0;attempt<200;++attempt){bool wide=(attempt&1)!=0,strip=(attempt&2)!=0;unsigned primitives=1+attempt%5,n=strip?primitives+2:primitives*3;std::vector<UINT> original(n);random.ib.desc.Format=wide?D3DFMT_INDEX32:D3DFMT_INDEX16;random.ib.bytes.resize(n*(wide?4:2));
   for(unsigned i=0;i<n;++i){seed=1664525*seed+1013904223;UINT value=7+(seed%5);original[i]=value;if(wide)std::memcpy(random.ib.bytes.data()+i*4,&value,4);else{uint16_t shortValue=uint16_t(value);std::memcpy(random.ib.bytes.data()+i*2,&shortValue,2);}}
   random.vb[0].bytes[8+5*24+4]^=1;Draw current{strip?D3DPT_TRIANGLESTRIP:D3DPT_TRIANGLELIST,-3,7,5,0,primitives,true};assert(snapshots.read(&random,&random.decl,current,copy));
   assert(copy.topology==current.topology&&copy.primitiveCount==primitives&&copy.indices.size()==n);
   for(unsigned i=0;i<n;++i)for(unsigned stream=0;stream<2;++stream){UINT vertex=copy.indices[i];assert(vertex<copy.vertexCount);unsigned extent=stream?4:16;for(unsigned byte=0;byte<extent;++byte)assert(copy.streams[stream].bytes[vertex*random.stride[stream]+byte]==random.vb[stream].bytes[random.offset[stream]+(original[i]-3)*random.stride[stream]+byte]);}
  }
 }

 // Consecutive runs, gaps, exact final-attribute boundary, and an invalid draw
 // between two content-cache hits must all preserve CURRENT stream data.
 {Device runs;Frame capture;Mesh copy;Draw selected{D3DPT_TRIANGLELIST,0,2,8,0,2,true};
  runs.indices({2,3,4,7,8,9},true);for(unsigned stream=0;stream<2;++stream)runs.vb[stream].bytes.resize(runs.offset[stream]+9*runs.stride[stream]+(stream?4:16));
  for(unsigned iteration=0;iteration<3;++iteration){runs.vb[0].bytes[runs.offset[0]+3*runs.stride[0]+4]^=3;assert(capture.read(&runs,&runs.decl,selected,copy));
   const unsigned sources[]={2,3,4,7,8,9};assert(copy.vertexCount==6);
   for(unsigned stream=0;stream<2;++stream){unsigned extent=stream?4:16;for(unsigned i=0;i<6;++i)for(unsigned byte=0;byte<extent;++byte)assert(copy.streams[stream].bytes[i*runs.stride[stream]+byte]==runs.vb[stream].bytes[runs.offset[stream]+sources[i]*runs.stride[stream]+byte]);
    for(unsigned byte=extent;byte<runs.stride[stream];++byte)assert(copy.streams[stream].bytes[5*runs.stride[stream]+byte]==0);}
   runs.indices({2,3,4,7,8,10},true);assert(!capture.read(&runs,&runs.decl,selected,copy,&why)&&why.error==Error::IndexRange);runs.indices({2,3,4,7,8,9},true);
  }
 }
 // Non-DYNAMIC does not imply immutable: it must also be copied every draw.
 d.vb[0].desc.Usage=d.ib.desc.Usage=0;d.vb[0].bytes[8+4*24+4]^=13;assert(frame.read(&d,&d.decl,draw,b));assert(b.streams[0].bytes!=old);
 d.indices({999,6,7,8});assert(!frame.read(&d,&d.decl,draw,b,&why)&&why.error==Error::IndexRange&&b.indices.empty());d.indices({999,7,8,9});
 draw.base=-8;assert(!frame.read(&d,&d.decl,draw,b,&why)&&why.error==Error::VertexRange);draw.base=-3;
 d.freq[1]=0x40000001;assert(!frame.read(&d,&d.decl,draw,b,&why)&&why.error==Error::Instancing);d.freq[1]=1;
 d.vb[0].failLock=true;assert(!frame.read(&d,&d.decl,draw,b,&why)&&why.error==Error::Lock);d.vb[0].failLock=false;
 d.vb[0].failUnlock=true;assert(!frame.read(&d,&d.decl,draw,b,&why)&&why.error==Error::Unlock);d.vb[0].failUnlock=false;
 Frame tiny(1);assert(!tiny.read(&d,&d.decl,draw,b,&why)&&why.error==Error::Budget);
 d.decl.e[1].Stream=4;assert(!frame.read(&d,&d.decl,draw,b,&why)&&why.error==Error::Declaration);d.decl.e[1].Stream=1;
 // A legal VB may end after the final attribute, before unused stride padding.
 d.vb[0].bytes.resize(8+6*24+16);assert(frame.read(&d,&d.decl,draw,b));for(unsigned i=64;i<72;++i)assert(b.streams[0].bytes[i]==0);
 d.vb[0].bytes.pop_back();assert(!frame.read(&d,&d.decl,draw,b,&why)&&why.error==Error::VertexRange);
 d.vb[0].bytes.resize(512);draw={D3DPT_TRIANGLESTRIP,0,0,0,4,2,false};assert(frame.read(&d,&d.decl,draw,b)&&b.vertexCount==4&&b.indices.empty());
 // UP starts at vertex zero and min=7 must select actual vertex seven.
 d.decl.e.erase(d.decl.e.begin()+1);draw={D3DPT_TRIANGLESTRIP,0,7,4,0,2,true};std::uint16_t ii[]={7,8,9,10};std::vector<std::uint8_t> up(11*24);for(unsigned i=0;i<up.size();++i)up[i]=std::uint8_t(i);
 assert(frame.readUP(&d.decl,draw,ii,D3DFMT_INDEX16,up.data(),24,a));assert(a.streams[0].bytes[4]==up[7*24+4]&&a.indices==std::vector<std::uint32_t>({0,1,2,3}));
 auto before=a.streams[0].bytes;up[7*24+4]^=3;assert(frame.readUP(&d.decl,draw,ii,D3DFMT_INDEX16,up.data(),24,b));assert(a.streams[0].bytes==before&&b.streams[0].bytes!=before);
 // Sparse UP data shares the remapping, tail-padding and priority contract.
 {d.decl.e={{0,4,2,0,0,0},{0xff,0,17,0,0,0}};std::uint32_t sparseIndices[]={7,10,7};Draw sparseDraw{D3DPT_TRIANGLELIST,0,7,4,0,1,true};Frame upFrame(160,80);Mesh copy;
  assert(upFrame.readUP(&d.decl,sparseDraw,sparseIndices,D3DFMT_INDEX32,up.data(),24,copy));assert(copy.vertexCount==2&&copy.indices==std::vector<std::uint32_t>({0,1,0}));assert(copy.streams[0].bytes[24+4]==up[10*24+4]);
  assert(!upFrame.readUP(&d.decl,sparseDraw,sparseIndices,D3DFMT_INDEX32,up.data(),24,copy,&why)&&why.error==Error::Budget);
  assert(upFrame.readUP(&d.decl,sparseDraw,sparseIndices,D3DFMT_INDEX32,up.data(),24,copy,&why,true));}
 draw.primitives=0xffffffff;assert(!frame.readUP(&d.decl,draw,ii,D3DFMT_INDEX16,up.data(),24,b));
 assert(d.ib.refs==1&&d.ib.locks==d.ib.unlocks);for(auto& v:d.vb)assert(v.refs==1&&v.locks==v.unlocks);assert(d.decl.refs==1);
 frame.clearFrame();assert(frame.bytesRead()==0);std::puts("snapshot mutation, multisource, dynamic/static, base/stride, format, UP, failure, lifetime tests passed");
}
'''
with tempfile.TemporaryDirectory(prefix='northlight-draw-snapshot-') as tmp:
 p=Path(tmp);(p/'d3d9.h').write_text(stub);(p/'test.cpp').write_text(harness)
 for flags in (['-O2'],['-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer']):
  subprocess.run(['clang++','-std=c++17','-Wall','-Wextra','-Werror',*flags,'-I',str(p),*fp.test_include_flags(),str(p/'test.cpp'),'-o',str(p/'test')],check=True)
  subprocess.run([str(p/'test')],check=True)
