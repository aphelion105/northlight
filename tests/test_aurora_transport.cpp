#include "world_dynamic_probes.h"
#include <cassert>
#include <cstdio>
#include <limits>
using namespace NorthlightGI;
using Source=NorthlightAuroraSource::Snapshot;
static void quad(WorldScene& s,Vec3 a,Vec3 b,Vec3 c,Vec3 d){unsigned i=unsigned(s.vertices.size());Vec3 n=normalized(cross(b-a,c-a));for(auto p:{a,b,c,d})s.vertices.push_back({p,n,0,0});s.triangles.push_back({i,i+1,i+2,0});s.triangles.push_back({i,i+2,i+3,0});}
static BVH room(bool closed){WorldScene s;s.materials.push_back({});s.materials.back().albedo=closed?Vec3(0,0,0):Vec3(.7f,.18f,.1f);
quad(s,{-5,-5,0},{5,-5,0},{5,5,0},{-5,5,0});
quad(s,{-5,-5,0},{-5,-5,8},{5,-5,8},{5,-5,0});
if(closed){quad(s,{-5,-5,8},{-5,5,8},{5,5,8},{5,-5,8});quad(s,{5,5,0},{5,5,8},{-5,5,8},{-5,5,0});quad(s,{-5,5,0},{-5,5,8},{-5,-5,8},{-5,-5,0});quad(s,{5,-5,0},{5,-5,8},{5,5,8},{5,5,0});}
BVH b;std::string error;assert(b.build(std::move(s),error));return b;}
static void equal(const Probe&a,const Probe&b){assert(a.valid==b.valid&&a.samples==b.samples&&a.backFaceSamples==b.backFaceSamples);for(unsigned j=0;j<4;++j){const auto d=a.sh[j]-b.sh[j];assert(dot(d,d)<2e-8f);}for(unsigned j=0;j<6;++j){assert(a.moments[j].mean==b.moments[j].mean);assert(a.moments[j].meanSquare==b.moments[j].meanSquare);}}
static std::shared_ptr<Source> source(unsigned phase){auto s=std::make_shared<Source>();s->complete=true;s->revision=phase+1;s->selection=7;for(unsigned i=0;i<NorthlightAuroraSource::SectorCount;++i)s->radiance[i]={float((i+phase)%7)*.06f,float((i*3+phase)%11)*.08f,float((i*5+phase)%13)*.07f};return s;}
int main(){auto open=room(false),sealed=room(true);Lighting l;l.skyRadiance={.1f,.12f,.16f};l.sunIrradiance={.4f,.5f,.6f};l.maxBounces=3;l.aurora=source(0);auto before=prepareLighting(l);assert(before.valid());
unsigned comparisons=0;
for(auto point:{Vec3(0,0,3),Vec3(40,40,3)})for(unsigned rays:{32u,256u,2048u}){
ProbeResponse response;AngularSkyResponse angular;auto p=solveProbeResponse(open,point,before,rays,73,response,&angular);assert(p.valid);equal(p,solveProbePrepared(open,point,before,rays,73));
for(unsigned phase=1;phase<8;++phase){Lighting changed=l;changed.aurora=source(phase);changed.skyRadiance={.02f,.08f,.1f};const auto after=prepareLighting(changed);equal(solveProbePrepared(open,point,after,rays,73),relightProbe(response,after,&angular));++comparisons;}
Lighting off=l;off.aurora.reset();equal(solveProbePrepared(open,point,prepareLighting(off),rays,73),relightProbe(response,prepareLighting(off),&angular));
ProbeResponse fixed;auto baked=solveProbeResponse(open,point,before,rays,73,fixed);equal(baked,relightProbe(fixed,before));}
auto dark=solveProbePrepared(sealed,{0,0,3},before,256,73);assert(dark.valid&&dot(dark.sh[0],dark.sh[0])==0);
// Directional response: a source in +X cannot illuminate opposite normals equally.
Lighting side;side.sunIrradiance={};side.skyRadiance={};auto one=std::make_shared<Source>();one->complete=true;one->revision=1;one->selection=3;one->radiance[NorthlightAuroraSource::sector(1,0,.4f)]={.1f,2.f,.3f};side.aurora=one;auto directional=solveProbePrepared(open,{40,40,3},prepareLighting(side),4096,73);auto facing=evaluateIrradiance(directional,{1,0,0}),away=evaluateIrradiance(directional,{-1,0,0});assert(facing.y>away.y*4&&facing.y>.05f);
// Cached recolouring does not flush transport; enabling/disabling changes only storage mode.
ProbeRelightCache cache;cache.configure(before,256);ProbeGridKey key{0,0,1};cache.solve(open,key,{0,0,8},before,256,73);const auto stats=cache.stats();assert(stats.bytes>0&&stats.bytes<=ProbeRelightCache::BudgetBytes);
Lighting other=l;other.aurora=source(5);auto after=prepareLighting(other);cache.configure(after,256);auto hit=cache.solve(open,key,{0,0,8},after,256,73);equal(hit,solveProbePrepared(open,{0,0,8},after,256,73));assert(cache.stats().hits==stats.hits+1&&cache.stats().resets==stats.resets);
Lighting off=l;off.aurora.reset();auto disabled=prepareLighting(off);cache.configure(disabled,256);auto offHit=cache.solve(open,key,{0,0,8},disabled,256,73);equal(offHit,solveProbePrepared(open,{0,0,8},disabled,256,73));assert(cache.stats().bytes<=ProbeRelightCache::BudgetBytes);cache.configure(before,256);equal(cache.solve(open,key,{0,0,8},before,256,73),solveProbePrepared(open,{0,0,8},before,256,73));
assert(!sameStaticLighting(l,other)&&!sameStaticLighting(l,off));auto unchanged=std::make_shared<Source>(*l.aurora);++unchanged->revision;other.aurora=unchanged;assert(sameStaticLighting(l,other));++unchanged->selection;assert(!sameStaticLighting(l,other));
for(unsigned bad=0;bad<4;++bad){auto invalid=source(0);if(bad==0)invalid->complete=false;if(bad==1)invalid->selection=0;if(bad==2)invalid->radiance[17][1]=-1;if(bad==3)invalid->radiance[3][2]=std::numeric_limits<float>::quiet_NaN();other.aurora=invalid;assert(!prepareLighting(other).valid());}
// Moving-path replay retains exactly the same directional emission.
StaticPathRecord record;MovingSolveStats movingStats;auto replay=solveProbeMoving(open,{0,0,3},before,256,73,sealed,record,1,&movingStats);equal(replay,solveProbePrepared(open,{0,0,3},before,256,73,&sealed));auto moved=solveProbeMoving(open,{0,0,3},after,256,73,sealed,record,2,&movingStats);equal(moved,solveProbePrepared(open,{0,0,3},after,256,73,&sealed));
printf("PASS angular aurora: %u cached/fresh comparisons, directional normals, sealed roof, cache <=4MiB, activation/removal, invalid admission, moving replay\n",comparisons);
}
