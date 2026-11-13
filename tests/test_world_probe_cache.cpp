#include "world_probe_cache.h"
#include <cassert>
#include <cstdio>
#include <limits>

using namespace NorthlightGI;

static Probe solved(ProbeGridKey key) {
    Probe p;p.position=Vec3(float(key.x)*8,float(key.y)*8,float(key.z)*8);
    p.valid=(key.x+key.y+key.z)%3!=0;
    p.samples=64;p.sh[0]=Vec3(float(key.x),float(key.y),float(key.z));
    p.moments[3].mean=float(key.z);return p;
}

struct Counts {unsigned hits=0,solves=0;};
static Counts grid(ProbeCache& cache,int originX) {
    Counts count;
    for(int z=-3;z<5;++z)for(int y=-1318;y<-1310;++y)for(int x=originX;x<originX+8;++x) {
        ProbeGridKey key{x,y,z};Probe p;
        if(cache.get(key,p))++count.hits;
        else{++count.solves;p=solved(key);cache.put(key,p);}
        Probe expected=solved(key);
        assert(p.valid==expected.valid&&p.samples==64);
        assert(p.position.x==expected.position.x&&p.position.y==expected.position.y&&p.position.z==expected.position.z);
        assert(p.sh[0].x==float(x)&&p.sh[0].y==float(y)&&p.sh[0].z==float(z));
    }
    return count;
}

static size_t occupied(const std::vector<ProbeAtlasEntry>& atlas) {
    size_t count=0;for(const auto& slot:atlas)if(slot.occupied)++count;return count;
}
static void assertOriginalGridPresent(const std::vector<ProbeAtlasEntry>& atlas) {
    assert(atlas.size()==4096);
    for(int z=-3;z<5;++z)for(int y=-1318;y<-1310;++y)for(int x=-154;x<-146;++x) {
        ProbeGridKey key{x,y,z};const auto& slot=atlas[probeAtlasIndex(key)];
        assert(slot.occupied&&slot.key==key);
        const Probe expected=solved(key);
        assert(slot.probe.valid==expected.valid&&slot.probe.samples==64);
        assert(slot.probe.sh[0].x==float(x)&&slot.probe.sh[0].y==float(y)&&slot.probe.sh[0].z==float(z));
    }
}

int main() {
    ProbeCache cache;
    auto first=grid(cache,-154);assert(first.hits==0&&first.solves==512);
    auto initialAtlas=cache.atlas();assert(occupied(initialAtlas)==512);assertOriginalGridPresent(initialAtlas);
    auto shifted=grid(cache,-153);assert(shifted.hits==448&&shifted.solves==64&&cache.size()==576);
    auto shiftedAtlas=cache.atlas();assert(occupied(shiftedAtlas)==576);assertOriginalGridPresent(shiftedAtlas);
    auto returned=grid(cache,-154);assert(returned.hits==512&&returned.solves==0);
    auto returnedAtlas=cache.atlas();assert(occupied(returnedAtlas)==576);assertOriginalGridPresent(returnedAtlas);
    cache.clear();assert(cache.size()==0);
    assert(cache.atlas().size()==4096&&occupied(cache.atlas())==0);
    auto generation=grid(cache,-154);assert(generation.hits==0&&generation.solves==512);

    ProbeGridKey key{9,8,7};
    assert(probeGridKey(Vec3(-1232,-10544,24),key));
    assert((key==ProbeGridKey{-154,-1318,3}));
    const uint32_t originalSeed=uint32_t(int32_t(-154))*73856093u^
        uint32_t(int32_t(-1318))*19349663u^uint32_t(3)*83492791u;
    assert(probeSeed(key)==originalSeed);
    const ProbeGridKey before=key;
    for(Vec3 bad:{Vec3(.5f,0,0),Vec3(std::numeric_limits<float>::infinity(),0,0),
                 Vec3(0,std::numeric_limits<float>::quiet_NaN(),0),Vec3(1e30f,0,0)}) {
        assert(!probeGridKey(bad,key));assert(key==before);
    }

    ProbeCache tiny(3);Probe out;
    tiny.put({1,0,0},solved({1,0,0}));tiny.put({2,0,0},solved({2,0,0}));tiny.put({3,0,0},solved({3,0,0}));
    assert(tiny.get({1,0,0},out));
    tiny.put({4,0,0},solved({4,0,0}));
    assert(!tiny.get({2,0,0},out)&&tiny.size()==3);
    Probe invalid=solved({3,0,0});invalid.valid=false;invalid.samples=128;
    tiny.put({3,0,0},invalid);assert(tiny.size()==3);
    assert(tiny.get({3,0,0},out)&&!out.valid&&out.samples==128);
    tiny.put({5,0,0},solved({5,0,0}));assert(!tiny.get({1,0,0},out));
    out.samples=777;assert(!tiny.get({99,0,0},out)&&out.samples==777);

    ProbeCache bounded(1000000);assert(bounded.capacity()==ProbeCache::MaxCapacity);
    const int count=int(ProbeCache::MaxCapacity)+3808;
    for(int i=0;i<count;++i){bounded.put({i,0,0},solved({i,0,0}));assert(bounded.size()<=ProbeCache::MaxCapacity);}
    assert(bounded.size()==ProbeCache::MaxCapacity&&!bounded.get({0,0,0},out)&&bounded.get({count-1,0,0},out));
    ProbeCache minimum(0);assert(minimum.capacity()==1);

    assert(probeAtlasIndex({0,0,0})==0);
    assert(probeAtlasIndex({-1,-1,-1})==4095);
    assert(probeAtlasIndex({-16,-32,-48})==0);
    assert(probeAtlasIndex({-17,18,-19})==15+2*16+13*256);
    assert(probeAtlasIndex({std::numeric_limits<int32_t>::min(),0,0})==0);
    ProbeCache aliases;
    const ProbeGridKey oldKey{-1,-2,-3},newKey{15,-2,-3};
    assert(probeAtlasIndex(oldKey)==probeAtlasIndex(newKey));
    aliases.put(oldKey,solved(oldKey));
    Probe invalidNew=solved(newKey);invalidNew.valid=false;
    aliases.put(newKey,invalidNew);
    auto aliasAtlas=aliases.atlas();const auto& winner=aliasAtlas[probeAtlasIndex(oldKey)];
    assert(occupied(aliasAtlas)==1&&winner.occupied&&!winner.probe.valid);
    assert(winner.key==newKey&&!(winner.key==oldKey)); // Full-key metadata rejects the old query.
    assert(aliases.get(oldKey,out)); // Touching old coordinate makes it the newest resident.
    auto promotedAtlas=aliases.atlas();assert(promotedAtlas[probeAtlasIndex(oldKey)].key==oldKey);
    assert(aliasAtlas[probeAtlasIndex(oldKey)].key==newKey); // Exports own their snapshots.
    aliases.clear();assert(occupied(aliases.atlas())==0);
    std::puts("PASS rolling world probe cache: 448 hits/64 new; return orbit 512 hits; invalid probes retained");
    std::puts("PASS generation clear, stable negative-coordinate seeds, key validation, LRU update/eviction, 8192 hard cap");
    std::puts("PASS fixed world atlas residency across scroll/return, negative wrap, newest alias metadata, invalid occupancy, clear");
}
