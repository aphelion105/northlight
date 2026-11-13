#include "probe_texture_upload.h"
#include "world_dynamic_probes.h"
#include <cassert>
#include <cstdio>
#include <thread>
using namespace NorthlightGI;
namespace V=LocalProbeVisibility;
static void quad(WorldScene& s,Vec3 centre,Vec3 u,Vec3 v){
    const uint32_t first=uint32_t(s.vertices.size());const Vec3 n=normalized(cross(u,v));
    for(const auto p:{centre-u-v,centre+u-v,centre+u+v,centre-u+v})s.vertices.push_back({p,n,0,0});
    s.triangles.push_back({first,first+1,first+2,0});s.triangles.push_back({first,first+2,first+3,0});
}
static BVH build(WorldScene scene){
    scene.materials.emplace_back();BVH b;std::string error;
    if(!b.build(std::move(scene),error)){std::fprintf(stderr,"BVH: %s\n",error.c_str());std::abort();}return b;
}
int main(){
    static_assert(sizeof(ProbeLocalVisibility)==648);
    for(float f:{0.f,1.f,-1.f,.04f,1.f/65536,1000.f,-1000.f}){
        const float round=V::scalar(V::half(f));assert(std::fabs(round-f)<=std::max(1e-7f,std::fabs(f)*.0005f));}
    for(unsigned y=0;y<V::Side;++y)for(unsigned x=0;x<V::Side;++x){const auto d=V::direction(x,y);assert(std::fabs(dot(d,d)-1)<1e-5f);}
    WorldScene s;quad(s,{2,0,0},{0,32,0},{0,0,32});auto wall=build(std::move(s));assert(wall.triangleCount()==2);
    auto map=V::build(wall,{0,0,0});
    assert(V::visibility(map,{4,0,0})==0&&V::visibility(map,{1,0,0})==1);
    assert(V::visibility(map,{-4,0,0})==1&&V::visibility(map,{0,0,0})==1);
    assert(V::visibility(std::shared_ptr<const ProbeLocalVisibility>{},{1,0,0})==0);
    unsigned behind=0;
    for(int y=-6;y<=6;++y)for(int z=-6;z<=6;++z){const Vec3 d={4,float(y),float(z)};
        assert(wall.occluded({0,0,0},d,std::sqrt(dot(d,d)),.0005f));
        assert(V::visibility(map,d)<.07f);++behind;}
    // Uniform flags may skip three reads only if the FP16 planes really agree.
    auto four=*map;for(auto& texel:four.texels)texel[3]=0;
    for(int y=-8;y<=8;++y)for(int z=-8;z<=8;++z){const Vec3 d={1.5f,float(y),float(z)};
        assert(std::fabs(V::visibility(*map,d)-V::visibility(four,d))<1e-6f);}
    s={};quad(s,{0,0,-1},{32,0,0},{0,32,0});auto floor=build(std::move(s));auto floorMap=V::build(floor,{});
    assert(V::visibility(floorMap,{7,7,0})>.99f&&V::visibility(floorMap,{0,0,-2})==0);
    s={};quad(s,{0,0,2},{32,0,0},{0,32,0});auto ceiling=build(std::move(s));
    assert(V::visibility(V::build(ceiling,{}),{0,0,4})==0);
    s={};quad(s,{2,0,0},{0,32,0},{0,0,32});quad(s,{0,2,0},{32,0,0},{0,0,32});quad(s,{0,0,2},{32,0,0},{0,32,0});
    auto corner=build(std::move(s));auto cornerMap=V::build(corner,{});
    for(unsigned i=0;i<33;++i){const float t=-1+float(i)/8;assert(V::visibility(cornerMap,{4,t,4})<.01f);}
    assert(V::visibility(V::build(wall,{4,0,0}),{-4,0,0})==0); // reverse side of a thin wall
    const Vec3 n=normalized({1,1,.2f}),u=normalized(cross(n,{0,0,1}))*32,v=normalized(cross(n,u))*32;
    s={};quad(s,n*2,u,v);auto slant=build(std::move(s));assert(V::visibility(V::build(slant,{}),n*4)<.01f);
    // A sub-cell door centered on +X must survive the low-resolution octahedron.
    s={};quad(s,{2,0,8.15f},{0,32,0},{0,0,7.85f});quad(s,{2,0,-8.15f},{0,32,0},{0,0,7.85f});
    quad(s,{2,8.15f,0},{0,7.85f,0},{0,0,.3f});quad(s,{2,-8.15f,0},{0,7.85f,0},{0,0,.3f});
    auto door=build(std::move(s));const auto doorMap=V::build(door,{});
    assert(V::visibility(doorMap,{4,0,0})>.99f&&V::visibility(doorMap,{4,4,0})<.01f);
    // Alpha cutouts use the same BVH material visibility as GI transport.
    s={};quad(s,{2,0,0},{0,32,0},{0,0,32});s.materials.emplace_back();
    s.materials[0].width=s.materials[0].height=1;s.materials[0].rgba={255,255,255,0};
    auto cutout=build(std::move(s));assert(V::visibility(V::build(cutout,{}),{4,0,0})==1);
    ProbeVisibilityCache cache;auto a=cache.get(wall,{0,0,0},{});auto b=cache.get(wall,{0,0,0},{});
    assert(a==b&&cache.stats().built==1&&cache.stats().reused==1);
    std::vector<std::thread> workers;for(unsigned i=0;i<4;++i)workers.emplace_back([&]{for(unsigned j=0;j<100;++j)assert(cache.get(wall,{0,0,0},{})==a);});
    for(auto& worker:workers)worker.join();assert(cache.stats().built==1);
    assert(!cache.get(wall,{0,0,0},{.1f,0,0}));
    assert(cache.get(wall,{16,0,0},{128,0,0})!=a); // wrapped slot, exact key
    assert(cache.get(wall,{0,0,0},{})!=a);
    auto newGeometry=cache.get(floor,{0,0,0},{});assert(newGeometry!=a&&V::visibility(newGeometry,{4,0,0})==1);
    NorthlightMemoryFootprint::Counter memory;cache.accountMemory(memory);const auto bytes=memory.total();cache.accountMemory(memory);assert(memory.total()==bytes);
    cache.reset();assert(cache.get(floor,{0,0,0},{})!=newGeometry);
    auto closed=V::overlay(floorMap,&wall,{});assert(closed!=floorMap&&V::visibility(closed,{4,0,0})==0);
    assert(V::overlay(floorMap,nullptr,{})==floorMap&&V::overlay(floorMap,&floor,{0,0,8})==floorMap);
    assert(V::visibility(floorMap,{4,0,0})==1); // moving blocker cannot mutate base
    assert(V::facing({1,0,0},{1,0,0})==0&&V::facing({1,0,0},{-1,0,0})==1);
    for(unsigned n:{16u,22u,42u}){const auto atlas=V::atlas(n);assert(atlas.width<=4096&&atlas.height<=4096&&atlas.columns*(atlas.height/9)>=n*n*n);}
    assert(V::atlas(0).columns==0&&V::atlas(43).columns==0);
    // Production uploader: padded pitch, initial missing tiles, no colour-only
    // writes, one dirty tile on replacement, and reset/retry after a GPU failure.
    const auto layout=V::atlas(4);const size_t pitch=layout.width*8+24;
    std::vector<unsigned char> pixels(pitch*layout.height,0x73);std::vector<ProbeAtlasEntry> atlas(64);
    atlas[13].occupied=atlas[13].probe.valid=true;atlas[13].probe.localVisibility=map;
    NorthlightProbeUpload::LocalVisibilityMirror mirror;assert(mirror.prepare(atlas.size())&&mirror.changed(atlas));
    auto dirty=mirror.write(pixels.data(),pitch,4,atlas);assert(dirty.left==0&&dirty.top==0&&dirty.right==layout.width&&dirty.bottom==layout.height);
    assert(!mirror.changed(atlas));atlas[13].probe.sh[0]={8,4,2};assert(!mirror.changed(atlas));
    const unsigned tx=13%layout.columns*9,ty=13/layout.columns*9;
    for(unsigned y=0;y<layout.height;++y){for(unsigned x=0;x<layout.width;++x){std::array<uint16_t,4> p;std::memcpy(p.data(),pixels.data()+y*pitch+x*8,8);
        if(x>=tx&&x<tx+9&&y>=ty&&y<ty+9)assert(p==map->texels[(y-ty)*9+x-tx]);else assert((p==std::array<uint16_t,4>{0,0,0,0xbc00}));}
        for(size_t x=layout.width*8;x<pitch;++x)assert(pixels[y*pitch+x]==0x73);}
    atlas[13].probe.localVisibility=floorMap;assert(mirror.changed(atlas));dirty=mirror.write(pixels.data(),pitch,4,atlas);
    assert(dirty.left==tx&&dirty.top==ty&&dirty.right==tx+9&&dirty.bottom==ty+9);
    atlas[13].occupied=false;assert(mirror.changed(atlas));mirror.write(pixels.data(),pitch,4,atlas);
    assert(!mirror.changed(atlas));std::array<uint16_t,4> cleared;std::memcpy(cleared.data(),pixels.data()+ty*pitch+tx*8,8);assert(cleared[3]==0xbc00);
    mirror.reset();assert(mirror.changed(atlas)&&mirror.prepare(atlas.size()));dirty=mirror.write(pixels.data(),pitch,4,atlas);assert(dirty.right==layout.width);
    // Probe publications share immutable visibility through cache copies and
    // retain structural blockers while actor radiance changes independently.
    ProbeCache colours;Probe colour;colour.valid=true;colour.localVisibility=map;colours.put({},colour);
    auto publication=colours.atlas();colours.clear();assert(publication[0].probe.localVisibility==map);
    DynamicProbeLayer moving;moving.reset(&wall.scene());unsigned reused=0,solved=0;
    assert(moving.apply(publication,[&](ProbeGridKey,Vec3,StaticPathRecord*){auto p=colour;p.sh[0]={1,1,1};p.localVisibility=floorMap;return p;},[]{return false;},reused,solved));
    assert(solved==1&&publication[0].probe.localVisibility==map);
    std::printf("LOCAL_VISIBILITY_PASS wall_samples=%u FP16=1 door=1 grazing=1 cache=1 moving=1\n",behind);
}
