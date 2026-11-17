#include "world_gi.h"
#include <cassert>
#include <cstdio>
#include <cstring>
using namespace NorthlightGI;
static void bits(float x){uint32_t u;std::memcpy(&u,&x,4);std::printf("%08x",u);}
static void vector(Vec3 v){bits(v.x);bits(v.y);bits(v.z);}
static void emit(const Probe& p){
    vector(p.position);bits(p.maxDistance);
    for(const auto& sh:p.sh)vector(sh);
    for(const auto& m:p.moments){bits(m.mean);bits(m.meanSquare);}
    std::printf("/%u/%u/%u\n",p.samples,p.backFaceSamples,unsigned(p.valid));
}
static BVH floor(bool cutout,bool fast){
    WorldScene s;s.materials.emplace_back();auto& m=s.materials[0];
    m.albedo={.4f,.7f,.9f};
    if(cutout){m.width=m.height=2;m.rgba={255,64,32,0,32,128,255,255,64,255,32,255,32,64,255,0};}
    s.vertices={{{-32,-32,0},{0,0,1},0,0},{{32,-32,0},{0,0,1},3,0},
                {{32,32,0},{0,0,1},3,3},{{-32,32,0},{0,0,1},0,3}};
    s.triangles={{0,1,2,0},{0,2,3,0}};BVH b;std::string error;assert(b.build(std::move(s),error,fast));return b;
}
int main(){
    unsigned invalid=0,valid=0;
    for(bool fast:{false,true})for(bool cutout:{false,true}){
        auto b=floor(cutout,fast);auto moving=floor(!cutout,fast);
        for(unsigned bounces:{0u,1u,3u})for(unsigned rays:{6u,16u,32u,64u,65u,256u})for(uint32_t seed:{1u,71u})
        for(Vec3 p:{Vec3{0,0,-1},Vec3{0,0,.001f},Vec3{0,0,1},Vec3{32,32,4},Vec3{100,0,4}}){
            Lighting light;light.maxDistance=64;light.maxBounces=bounces;
            light.points.push_back({{2,2,8},{3,2,1},1,32});
            light.additionalDirections.push_back({normalized({-.2f,.3f,.8f}),{.2f,.3f,.1f}});
            const auto l=prepareLighting(light);assert(l.valid());
            auto direct=solveProbePrepared(b,p,l,rays,seed);emit(direct);valid+=direct.valid;invalid+=!direct.valid;
            ProbeResponse response;auto responseProbe=solveProbeResponse(b,p,l,rays,seed,response);emit(responseProbe);emit(response.fixed);
            for(const auto& source:response.sources)for(const auto& sh:source)vector(sh);
            emit(relightProbe(response,l));emit(solveProbePrepared(b,p,l,rays,seed,&moving));
        }
    }
    assert(valid&&invalid);std::printf("PREFLIGHT_CASES valid=%u invalid=%u\n",valid,invalid);
}
