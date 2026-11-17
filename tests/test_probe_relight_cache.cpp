#include "world_probe_cache.h"
#include <cassert>
#include <cstdio>
#include <cstring>
using namespace NorthlightGI;
// The relight cache frees its slots on release() (memory trim, map change) and allocates them again on the next
// configure(); in between every solve is the plain solver, with identical results.
int main(){
    BVH bvh;std::string error;assert(bvh.build({},error));
    Lighting light;light.sunDirection={0,0,1};light.sunIrradiance={1,1,1};light.skyRadiance={.2f,.3f,.4f};
    const PreparedLighting prepared=prepareLighting(light);assert(prepared.valid());
    const ProbeGridKey key{0,0,0};const Vec3 position{4,4,4};const unsigned rays=16;const uint32_t seed=probeSeed(key);
    const Probe direct=solveProbePrepared(bvh,position,prepared,rays,seed);
    ProbeRelightCache cache;cache.configure(prepared,rays);assert(cache.stats().bytes>0);
    const Probe first=cache.solve(bvh,key,position,prepared,rays,seed),second=cache.solve(bvh,key,position,prepared,rays,seed);
    assert(cache.stats().hits==1&&cache.stats().misses==1&&first.valid==direct.valid&&second.valid==direct.valid);
    cache.release();assert(cache.stats().bytes==0);
    const Probe released=cache.solve(bvh,key,position,prepared,rays,seed);
    assert(cache.stats().hits==1&&!std::memcmp(released.sh,direct.sh,sizeof direct.sh)); // no stale slot answers after release
    cache.configure(prepared,rays);assert(cache.stats().bytes>0);
    (void)cache.solve(bvh,key,position,prepared,rays,seed);(void)cache.solve(bvh,key,position,prepared,rays,seed);
    assert(cache.stats().hits==2);
    WorldScene s;s.materials.emplace_back();
    s.vertices={{{-64,-64,0},{0,0,1}},{{64,-64,0},{0,0,1}},{{64,64,0},{0,0,1}},{{-64,64,0},{0,0,1}}};
    s.triangles={{0,1,2,0},{0,2,3,0}};BVH floor;assert(floor.build(std::move(s),error));
    cache.reset();cache.configure(prepared,32);const ProbeGridKey buried{0,0,-1};const Vec3 below{0,0,-8};
    assert(!cache.solve(floor,buried,below,prepared,32,91).valid);
    auto changed=light;changed.sunDirection={1,0,.1f};changed.maxBounces=1;changed.rayBias=.01f;
    changed.worldUp={0,1,0};changed.points.push_back({{4,4,4},{3,2,1},1,32});
    auto next=prepareLighting(changed);cache.configure(next,32);auto stats=cache.stats();
    auto reused=cache.solve(floor,buried,below,next,32,91),plain=solveProbePrepared(floor,below,next,32,91);
    assert(!reused.valid&&cache.stats().misses==stats.misses&&cache.stats().invalidHits==stats.invalidHits+1);
    assert(!std::memcmp(reused.sh,plain.sh,sizeof plain.sh)&&!std::memcmp(reused.moments,plain.moments,sizeof plain.moments));
    assert(reused.samples==plain.samples&&reused.backFaceSamples==plain.backFaceSamples);
    // Lattice, reach and geometry changes still require a solve.
    cache.configure(next,64);stats=cache.stats();cache.solve(floor,buried,below,next,64,91);assert(cache.stats().misses==stats.misses+1);
    changed.maxDistance=16;next=prepareLighting(changed);cache.configure(next,64);stats=cache.stats();cache.solve(floor,buried,below,next,64,91);assert(cache.stats().misses==stats.misses+1);
    stats=cache.stats();cache.solve(floor,buried,below,next,64,92);assert(cache.stats().misses==stats.misses+1);
    cache.reset();cache.configure(next,64);stats=cache.stats();assert(cache.solve(bvh,buried,below,next,64,92).valid);assert(cache.stats().misses==stats.misses+1);
    std::puts("PASS relight cache: release frees the slots, plain solves meanwhile, configure allocates again");
}
