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
    std::puts("PASS relight cache: release frees the slots, plain solves meanwhile, configure allocates again");
}
