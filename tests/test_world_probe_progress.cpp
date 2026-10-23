#include "world_probe_progress.h"
#include "world_dynamic_probes.h"
#include "probe_activation.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <set>
using namespace NorthlightGI;
static Probe tagged(ProbeGridKey key,float light=1){Probe p;p.position={float(key.x)*8,float(key.y)*8,float(key.z)*8};p.samples=64;p.valid=true;p.sh[0]={light,light,light};for(auto& m:p.moments){m.mean=123;m.meanSquare=456;}return p;}
static void same(const Probe& a,const Probe& b){assert(a.valid==b.valid&&a.samples==b.samples);assert(!std::memcmp(&a.position,&b.position,sizeof(Vec3)));assert(!std::memcmp(a.sh,b.sh,sizeof a.sh));assert(!std::memcmp(a.moments,b.moments,sizeof a.moments));}
// 0.3.153 GIDistance layouts: window keys never share an atlas slot, corners
// first then nearest, the default cache holds >=1.5 windows, activation follows.
static void layoutCheck(unsigned n){
 const auto layout=probeLayoutFor(n);assert(configureProbeLayout(layout));const auto& l=probeLayout();
 assert(l.n==n&&l.nz==14&&l.atlas==std::max(n,14u)+2&&l.count()==n*n*14);
 {ProbeCache cache;assert(cache.capacity()==l.atlasSize()&&cache.capacity()*100>=size_t(l.count())*149);} /* 4096/2744 at the default */
 for(Vec3 camera:{Vec3{-129,-2048.5f,3},Vec3{7.99f,.1f,-10},Vec3{128,100,1000}}){
  const Vec3 origin=probeWindowOrigin(camera);auto order=probeSolveOrder(camera);assert(order.size()==l.count());std::set<unsigned> seen,slots;float previous=-1;
  for(unsigned cursor=0;cursor<order.size();++cursor){const unsigned i=order[cursor];assert(i<l.count()&&seen.insert(i).second);ProbeGridKey key;const Vec3 p=probeWindowPosition(origin,i);assert(probeGridKey(p,key));
   assert(slots.insert(unsigned(probeAtlasIndex(key))).second&&probeAtlasIndex(key)<l.atlasSize());
   if(cursor<8){const int cx=int(std::floor(camera.x/8)),cy=int(std::floor(camera.y/8)),cz=int(std::floor(camera.z/8));
    assert((key.x==cx||key.x==cx+1)&&(key.y==cy||key.y==cy+1)&&(key.z==cz||key.z==cz+1));}
   else{const Vec3 d=p-camera;assert(dot(d,d)>=previous);previous=dot(d,d);}}
  // Horizontal extent 4n-4 around the eye cell, vertical 14 cells as before.
  assert(origin.x==std::floor(camera.x/8)*8-float(n/2-1)*8&&origin.z==std::floor(camera.z/8)*8-48);}
 ProbeCache cache;for(unsigned i:probeSolveOrder({1,2,3})){ProbeGridKey key;assert(probeGridKey(probeWindowPosition(probeWindowOrigin({1,2,3}),i),key));cache.put(key,tagged(key));}
 unsigned occupied=0;for(const auto& e:cache.atlas())occupied+=e.occupied;assert(occupied==l.count());
 NorthlightProbeActivation activation;activation.begin("Azeroth");auto atlas=cache.atlas();for(size_t i=0;i<atlas.size();++i)activation.update(i,atlas[i],1);
}
int main(){
 {const ProbeLayout d;assert(d.n==14&&d.nz==14&&d.atlas==16&&d.count()==2744&&d.atlasSize()==4096&&ProbeCache().capacity()==4096);
  for(unsigned n:{10u,12u,16u,18u,20u,22u,14u})layoutCheck(n);
  ProbeLayout bad;bad.n=12;bad.atlas=14;assert(!configureProbeLayout(bad));bad=probeLayoutFor(24);assert(!configureProbeLayout(bad));bad=probeLayoutFor(15);assert(!configureProbeLayout(bad));
  assert(probeLayout().n==14&&probeLayout().atlas==16);}
 unsigned windows=0;
 for(float x:{-129.f,-128.f,-7.99f,0.f,7.99f,128.f})for(float y:{-2048.5f,-8.f,.1f,100.f})for(float z:{-10.f,0.f,3.f,8.f,1000.f}){
  Vec3 camera{x,y,z},origin=probeWindowOrigin(camera);auto order=probeSolveOrder(camera);std::set<unsigned> seen;float previous=-1;
  for(unsigned cursor=0;cursor<order.size();++cursor){unsigned i=order[cursor];assert(i<probeLayout().count()&&seen.insert(i).second);Vec3 p=probeWindowPosition(origin,i),delta=p-camera;ProbeGridKey key;assert(probeGridKey(p,key));
   if(cursor<8){assert(key.x==int(std::floor(x/8))||key.x==int(std::floor(x/8))+1);assert(key.y==int(std::floor(y/8))||key.y==int(std::floor(y/8))+1);assert(key.z==int(std::floor(z/8))||key.z==int(std::floor(z/8))+1);}
   else {float d=dot(delta,delta);assert(d>=previous);previous=d;}}
  assert(seen.size()==2744);++windows;
 }
 constexpr uint32_t I=ProbePublicationCadence::IntervalMs;
 ProbePublicationCadence cadence;assert(!cadence.due(100,8));cadence.solved();assert(!cadence.due(100,7)&&cadence.due(100,8));cadence.published(100);assert(!cadence.due(1000,2744));cadence.solved();assert(!cadence.due(100+I-1,8)&&cadence.due(100+I,8));cadence.published(100+I);
 // Rapid camera requests do not reset the generation's clock or dirty work.
 for(unsigned t=101+I;t<100+2*I;t+=3){cadence.solved();assert(!cadence.due(t,8));}assert(cadence.due(100+2*I,8));cadence.published(UINT32_MAX-100);cadence.solved();assert(!cadence.due(I-102,8)&&cadence.due(I-101,8));cadence.reset();cadence.solved();assert(cadence.due(1,8));
 ProbeCache oldCache,newCache;for(int z=-6;z<8;++z)for(int y=-6;y<8;++y)for(int x=-6;x<8;++x)oldCache.put({x,y,z},tagged({x,y,z},2));auto old=oldCache.atlas();NorthlightProbeActivation activation;activation.begin("Azeroth");for(size_t i=0;i<old.size();++i)activation.update(i,old[i],1);
 auto order=probeSolveOrder({1,2,3});Vec3 origin=probeWindowOrigin({1,2,3});for(unsigned i=0;i<8;++i){ProbeGridKey key;assert(probeGridKey(probeWindowPosition(origin,order[i]),key));newCache.put(key,tagged(key,3));}
 auto partial=newCache.atlas();retainProbeDisplayFallback(partial,old);unsigned occupied=0,replaced=0;for(size_t i=0;i<partial.size();++i)if(partial[i].occupied){++occupied;replaced+=partial[i].probe.sh[0].x==3;assert(activation.update(i,partial[i],2)==1);}assert(occupied==2744&&replaced==8&&newCache.size()==8);
 // Occupied invalid results and full-key modulo collisions supersede fallback.
 auto invalid=tagged({0,0,0});invalid.valid=false;newCache.put({0,0,0},invalid);newCache.put({16,0,1},tagged({16,0,1},4));partial=newCache.atlas();retainProbeDisplayFallback(partial,old);assert(!partial[probeAtlasIndex({0,0,0})].probe.valid);assert((partial[probeAtlasIndex({0,0,1})].key==ProbeGridKey{16,0,1}));assert(activation.update(probeAtlasIndex({0,0,1}),partial[probeAtlasIndex({0,0,1})],3)==3);
 // Every fully solved field is unchanged by order; cold and scrolled windows.
 BVH bvh;std::string error;assert(bvh.build({},error));Lighting light;auto prepared=prepareLighting(light);ProbeCache lex,progressive;
 for(Vec3 camera:{Vec3{1,2,3},Vec3{9,2,3},Vec3{-7,2,3}}){origin=probeWindowOrigin(camera);for(unsigned i=0;i<probeLayout().count();++i){auto p=probeWindowPosition(origin,i);ProbeGridKey key;assert(probeGridKey(p,key));Probe probe;if(!lex.get(key,probe))lex.put(key,solveProbe(bvh,p,light,64,probeSeed(key)));}
  for(unsigned i:probeSolveOrder(camera)){auto p=probeWindowPosition(origin,i);ProbeGridKey key;assert(probeGridKey(p,key));Probe probe;if(!progressive.get(key,probe))progressive.put(key,solveProbePrepared(bvh,p,prepared,64,probeSeed(key)));}
  auto a=lex.atlas(),b=progressive.atlas();for(unsigned i=0;i<probeLayout().count();++i){ProbeGridKey key;assert(probeGridKey(probeWindowPosition(origin,i),key));const auto& x=a[probeAtlasIndex(key)];const auto& y=b[probeAtlasIndex(key)];assert(x.occupied&&y.occupied&&x.key==key&&y.key==key);same(x.probe,y.probe);}}
 // Cached overlay preserves exact moving correction without spending solves.
 WorldScene actor;actor.materials.push_back({});for(Vec3 p:{Vec3{-4,-4,0},Vec3{4,-4,0},Vec3{0,4,0}})actor.vertices.push_back({p,{0,0,1}});actor.triangles.push_back({0,1,2,0});DynamicProbeLayer dynamic;dynamic.reset(&actor);ProbeCache base;for(int x=-2;x<=2;++x)base.put({x,0,0},tagged({x,0,0}));auto final=base.atlas();unsigned hits=0,solves=0,calls=0;
 assert(dynamic.apply(final,[&](ProbeGridKey key,Vec3){++calls;return tagged(key,2);},[]{return false;},hits,solves));assert(calls>0);unsigned totalCalls=calls;auto generation=dynamic.generation();
 for(unsigned n=0;n<100;++n){auto cached=base.atlas();hits=0;assert(dynamic.applyCached(cached,[]{return false;},hits));assert(hits==totalCalls&&dynamic.generation()==generation&&calls==totalCalls);for(size_t i=0;i<cached.size();++i)if(cached[i].occupied)same(cached[i].probe,final[i].probe);}
 dynamic.observe(nullptr);auto departed=base.atlas();hits=0;assert(dynamic.applyCached(departed,[]{return false;},hits)&&hits==0);for(const auto& e:departed)if(e.occupied)assert(e.probe.sh[0].x==1);
 dynamic.reset(&actor);auto untouched=base.atlas();hits=0;assert(dynamic.applyCached(untouched,[]{return false;},hits)&&hits==0&&dynamic.retained()==0);hits=0;assert(!dynamic.applyCached(untouched,[]{return true;},hits)&&hits==0);
 std::printf("PASS %u complete nearest-first windows; eight interpolation corners; bounded wrap-safe cadence; GIDistance layouts 10..22 collision-free; display fallback preserves2744slots and activation ages; exact2744-probe transport across3windows; cached-only dynamic corrections\n",windows);
}
