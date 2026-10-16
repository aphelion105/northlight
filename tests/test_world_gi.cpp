#include "world_gi.h"
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <utility>

using namespace NorthlightGI;

static void quad(WorldScene& s,Vec3 a,Vec3 b,Vec3 c,Vec3 d,uint32_t material,float u=.5f,float v=.5f){
    uint32_t n=uint32_t(s.vertices.size());Vec3 normal=normalized(cross(b-a,c-a));
    for(Vec3 p:{a,b,c,d})s.vertices.push_back({p,normal,u,v});
    s.triangles.push_back({n,n+1,n+2,material});s.triangles.push_back({n,n+2,n+3,material});
}
static BVH build(WorldScene s){BVH b;std::string error;assert(b.build(std::move(s),error));assert(error.empty());return b;}
static bool near(float x,float y,float tolerance=.0001f){return std::fabs(x-y)<tolerance;}
static void box(WorldScene& s,float q,uint32_t m){
    quad(s,{-q,-q,-q},{q,-q,-q},{q,q,-q},{-q,q,-q},m);
    quad(s,{-q,-q,q},{-q,q,q},{q,q,q},{q,-q,q},m);
    quad(s,{-q,-q,-q},{-q,-q,q},{q,-q,q},{q,-q,-q},m);
    quad(s,{q,q,-q},{q,q,q},{-q,q,q},{-q,q,-q},m);
    quad(s,{-q,q,-q},{-q,q,q},{-q,-q,q},{-q,-q,-q},m);
    quad(s,{q,-q,-q},{q,-q,q},{q,q,q},{q,q,-q},m);
}

static void testOcclusion(){
    WorldScene s;s.materials.push_back({});
    quad(s,{-2,-2,2},{2,-2,2},{2,2,2},{-2,2,2},0);
    BVH b=build(std::move(s));
    auto h=b.trace({0,0,0},{0,0,4});assert(h.hit&&near(h.distance,2));
    assert(h.normal.z<-.99f&&h.geometricNormal.z<-.99f);
    assert(b.occluded({0,0,0},{0,0,1},3));assert(!b.occluded({0,0,0},{0,0,1},1));
    assert(!b.occluded({5,0,0},{0,0,1},3));assert(!b.occluded({0,0,0},{0,0,0},3));
    assert(!b.trace({0,0,0},{0,0,1},3,1).hit);
    auto back=b.trace({0,0,4},{0,0,-1});assert(back.hit&&back.normal.z>.99f);
    std::puts("PASS opaque occlusion, normalized rays, two-sided normals and bounds");
}
static void testAlpha(){
    WorldScene s;WorldMaterial cutout;cutout.albedo={1,1,1};cutout.width=2;cutout.height=1;
    cutout.rgba={255,0,0,0, 0,255,0,255};s.materials.push_back(cutout);s.materials.push_back({});
    // All triangle vertices sample the exact transparent texel center.
    quad(s,{-2,-2,1},{2,-2,1},{2,2,1},{-2,2,1},0,.25f,.5f);
    quad(s,{-2,-2,3},{2,-2,3},{2,2,3},{-2,2,3},1);
    BVH hole=build(s);auto h=hole.trace({0,0,0},{0,0,1});assert(h.hit&&near(h.distance,3));
    for(unsigned i=0;i<4;++i)s.vertices[i].u=-.25f; // Wrap to opaque .75.
    BVH opaque=build(std::move(s));h=opaque.trace({0,0,0},{0,0,1});
    assert(h.hit&&near(h.distance,1));assert(h.albedo.y>.99f&&h.albedo.x<.001f);
    std::puts("PASS texture alpha holes, wrapped UVs and material color");
}
static void testSamplerModes(){
    auto sample=[](unsigned uMode,unsigned vMode,float u,float v,bool cutout=false){
        WorldScene scene;WorldMaterial m;m.albedo={1,1,1};m.width=2;m.height=2;m.addressU=uMode;m.addressV=vMode;
        m.rgba={255,0,0,255, 0,255,0,uint8_t(cutout?0:255), 0,0,255,255, 255,255,255,255};scene.materials.push_back(m);
        quad(scene,{-1,-1,1},{1,-1,1},{1,1,1},{-1,1,1},0,u,v);return build(std::move(scene)).trace({0,0,0},{0,0,1});
    };
    assert(sample(1,1,-.25f,.25f).albedo.y>.99f); // wrap -> upper-right green
    assert(sample(2,1,-.25f,.25f).albedo.x>.99f); // mirror -> upper-left red
    assert(sample(3,1,-.25f,.25f).albedo.x>.99f); // clamp -> left edge red
    assert(sample(1,1,2.25f,.25f).albedo.x>.99f);
    assert(sample(2,1,2.25f,.25f).albedo.x>.99f);
    assert(sample(3,1,2.25f,.25f).albedo.y>.99f);
    assert(sample(2,1,1.25f,.25f).albedo.y>.99f);
    assert(sample(3,3,.25f,-100000.f).albedo.x>.99f);
    assert(sample(1,1,.25f,-.25f).albedo.z>.99f);
    assert(sample(1,2,.25f,-.25f).albedo.x>.99f);
    assert(sample(1,3,.25f,1.25f).albedo.z>.99f);
    auto seam=sample(1,1,0,.25f);assert(near(seam.albedo.x,.5f)&&near(seam.albedo.y,.5f));
    assert(sample(2,1,0,.25f).albedo.x>.99f);assert(sample(3,1,0,.25f).albedo.x>.99f);
    assert(!sample(1,1,-.25f,.25f,true).hit);assert(sample(2,1,-.25f,.25f,true).hit);assert(sample(3,1,-.25f,.25f,true).hit);
    WorldScene invalid;invalid.materials.push_back({});invalid.materials[0].addressU=4;BVH b;std::string error;assert(!b.build(std::move(invalid),error));
    std::puts("PASS independent wrap/mirror/clamp U/V, negative and distant UVs, bilinear edge semantics and alpha visibility");
}
static void testTracerInvariants(){
    // Exactly coincident duplicates: the lowest triangle index wins at equal
    // distance, whatever the tree shape (0.3.138 order-independent tie rule).
    WorldScene s;WorldMaterial green,red;green.albedo={0,1,0};red.albedo={1,0,0};s.materials={green,red};
    quad(s,{-2,-2,2},{2,-2,2},{2,2,2},{-2,2,2},0);
    uint32_t seed=7;auto random=[&](){seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;return float(seed>>8)/16777216.f*2-1;};
    for(unsigned i=0;i<3000;++i){Vec3 c(random()*40,random()*40,random()*40+10);
        uint32_t n=uint32_t(s.vertices.size());for(unsigned k=0;k<3;++k)s.vertices.push_back({c+Vec3(random(),random(),random()),{0,0,1},.5f,.5f});
        s.triangles.push_back({n,n+1,n+2,uint32_t(i%2)});}
    quad(s,{-2,-2,2},{2,-2,2},{2,2,2},{-2,2,2},1);
    BVH b=build(s);auto h=b.trace({.3f,.2f,0},{0,0,1});assert(h.hit&&h.triangle<2&&h.albedo.y>.99f);
    BVH legacy;{std::string error;WorldScene copy=s;assert(legacy.build(std::move(copy),error,false));}
    // Closest hit equals the brute-force minimum over single-triangle scenes.
    std::vector<BVH> single;single.reserve(s.triangles.size());
    for(const auto& t:s.triangles){WorldScene one;one.materials=s.materials;for(uint32_t v:{t.v0,t.v1,t.v2})one.vertices.push_back(s.vertices[v]);one.triangles.push_back({0,1,2,t.material});single.push_back(build(std::move(one)));}
    for(unsigned r=0;r<400;++r){
        Vec3 o(random()*50,random()*50,random()*20-5),d(random(),random(),random()+.5f);
        RayHit best;best.distance=200;uint32_t id=UINT32_MAX;
        for(uint32_t i=0;i<single.size();++i){auto x=single[i].trace(o,d,.001f,200);if(x.hit&&(x.distance<best.distance||(x.distance==best.distance&&i<id))){best=x;id=i;}}
        auto x=b.trace(o,d,.001f,200);assert(x.hit==(id!=UINT32_MAX));
        if(x.hit){assert(x.triangle==id&&x.distance==best.distance&&x.albedo.x==best.albedo.x&&x.normal.z==best.normal.z);}
        assert(b.occluded(o,d,200,.001f)==x.hit);
        // GIFastBVH=0 median tree: same hit set and distances (ties may pick another index).
        auto y=legacy.trace(o,d,.001f,200);assert(y.hit==x.hit&&(!y.hit||y.distance==x.distance));assert(legacy.occluded(o,d,200,.001f)==y.hit);
    }
    std::puts("PASS order-independent closest hit, lowest-index ties, brute-force and legacy-tree equality");
}
static void testSun(){
    WorldScene s;WorldMaterial white;white.albedo={1,1,1};s.materials.push_back(white);
    quad(s,{-100,-100,0},{100,-100,0},{100,100,0},{-100,100,0},0);
    Lighting l;l.sunDirection={0,0,1};l.sunIrradiance={1,1,1};l.skyRadiance={0,0,0};l.maxDistance=30;l.maxBounces=1;
    auto b=build(s);auto p=solveProbe(b,{0,0,1},l,4096,73);
    Vec3 lit=evaluateIrradiance(p,{0,0,-1});assert(lit.x>.9f&&lit.x<1.1f);
    // Closed box blocks every direct ray and every escape path, even with
    // white materials and three surface bounces. There is no emission.
    WorldScene closed;closed.materials.push_back(white);box(closed,4,0);b=build(std::move(closed));
    l.maxBounces=3;l.skyRadiance={1,1,1};p=solveProbe(b,{0,0,0},l,2048,73);
    for(const auto& c:p.sh)assert(c.x==0&&c.y==0&&c.z==0);
    assert(b.occluded({0,0,0},l.sunDirection));
    std::puts("PASS Lambertian direct sun energy and enclosed sky/sun blocking");
}
static void testBounce(){
    WorldScene s;WorldMaterial floor;floor.albedo={.75f,.75f,.75f};s.materials.push_back(floor);
    WorldMaterial red;red.albedo={.85f,.025f,.025f};s.materials.push_back(red);
    quad(s,{-10,-10,0},{0,-10,0},{0,10,0},{-10,10,0},0);
    quad(s,{0,-10,0},{0,-10,10},{0,10,10},{0,10,0},1);
    BVH b=build(std::move(s));Lighting l;l.sunIrradiance={0,0,0};l.skyRadiance={1,1,1};l.maxDistance=50;
    l.maxBounces=1;Probe one=solveProbe(b,{-1,0,1},l,8192,91);
    l.maxBounces=3;Probe three=solveProbe(b,{-1,0,1},l,8192,91);
    Vec3 low=evaluateIrradiance(one,{1,0,-.3f}),high=evaluateIrradiance(three,{1,0,-.3f});
    std::printf("bounce test: one=(%.5f %.5f %.5f), three=(%.5f %.5f %.5f)\n",low.x,low.y,low.z,high.x,high.y,high.z);
    assert(high.x>high.y*1.2f);assert(high.x>low.x+.015f);
    for(const auto& c:three.sh)assert(std::isfinite(c.x)&&std::isfinite(c.y)&&std::isfinite(c.z));
    std::puts("PASS colored wall transport and added actual diffuse bounces");
}
static void testSkyAndMoments(){
    BVH empty=build({});Lighting l;l.sunIrradiance={0,0,0};l.skyRadiance={1,1,1};l.maxDistance=100;
    auto p=solveProbe(empty,{0,0,0},l,4096,44);Vec3 e=evaluateIrradiance(p,{0,0,1});
    assert(near(e.x,3.14159265f,.005f));
    for(auto m:p.moments){assert(near(m.mean,100));assert(near(m.meanSquare,10000,.01f));}
    auto repeated=solveProbe(empty,{0,0,0},l,4096,44);
    for(unsigned i=0;i<4;++i)assert(p.sh[i].x==repeated.sh[i].x&&p.sh[i].y==repeated.sh[i].y&&p.sh[i].z==repeated.sh[i].z);
    // Moment function accepts a known zero-variance wall at distance 2.
    for(auto& m:p.moments){m.mean=2;m.meanSquare=4;}
    assert(probeVisibility(p,{1,0,0},1)>.99f);assert(probeVisibility(p,{1,0,0},4)<.001f);
    assert(!solveProbe(empty,{0,0,0},l,0).valid);
    std::puts("PASS analytic open-sky irradiance, deterministic sampling and leak rejection");
}
static void testProbeValidity(){
    WorldScene s;s.materials.push_back({});quad(s,{-100,-100,0},{100,-100,0},{100,100,0},{-100,100,0},0);
    BVH b=build(std::move(s));Lighting l;
    auto above=solveProbe(b,{0,0,1},l,32),below=solveProbe(b,{0,0,-1},l,32),close=solveProbe(b,{0,0,.01f},l,32);
    assert(above.valid&&above.backFaceSamples==0);assert(!below.valid&&below.backFaceSamples>=15);assert(!close.valid);
    for(const auto& c:below.sh)assert(c.x==0&&c.y==0&&c.z==0);
    WorldScene closed;closed.materials.push_back({});box(closed,2,0);
    // box() uses inward geometric winding; vertex normals establish the
    // original outward side regardless of triangle index orientation.
    for(auto& v:closed.vertices)v.normal=-v.normal;
    b=build(std::move(closed));auto solid=solveProbe(b,{0,0,0},l,32);
    assert(!solid.valid&&solid.backFaceSamples==32);
    std::puts("PASS subterranean, inside-solid and near-surface probe rejection using outward normals");
}
static void u32(FILE* f,uint32_t n){unsigned char b[4]={uint8_t(n),uint8_t(n>>8),uint8_t(n>>16),uint8_t(n>>24)};assert(std::fwrite(b,1,4,f)==4);}
static void f32(FILE* f,float v){uint32_t i;std::memcpy(&i,&v,4);u32(f,i);}
static void vec(FILE* f,Vec3 v){f32(f,v.x);f32(f,v.y);f32(f,v.z);}
static void writeScene(const char* path,const WorldScene& s){
    FILE*f=std::fopen(path,"wb");assert(f);std::fwrite("FGS2",1,4,f);u32(f,2);u32(f,uint32_t(s.vertices.size()));u32(f,uint32_t(s.triangles.size()));u32(f,uint32_t(s.materials.size()));
    for(const auto&v:s.vertices){vec(f,v.position);vec(f,v.normal);f32(f,v.u);f32(f,v.v);}
    for(const auto&t:s.triangles){u32(f,t.v0);u32(f,t.v1);u32(f,t.v2);u32(f,t.material);}
    for(const auto&m:s.materials){vec(f,m.albedo);u32(f,m.width);u32(f,m.height);f32(f,m.alphaCutoff);u32(f,uint32_t(m.rgba.size()));std::fwrite(m.rgba.data(),1,m.rgba.size(),f);}
    std::fclose(f);
}
// Unique per run under the system temp dir, so parallel runs never share a file.
static std::filesystem::path scratchPath(const char* name){
    return std::filesystem::temp_directory_path()/(std::string(name)+"-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
}
static void testLoader(){
    const std::string pathName=scratchPath("northlight-gi-loader-test").string()+".fgs";const char* path=pathName.c_str();FILE* f=std::fopen(path,"wb");assert(f);
    assert(std::fwrite("FGS2",1,4,f)==4);u32(f,2);u32(f,0);u32(f,0);u32(f,0);std::fclose(f);
    WorldScene scene;std::string error;assert(loadScene(path,scene,error));
    f=std::fopen(path,"ab");assert(f);std::fputc('x',f);std::fclose(f);
    scene.materials.push_back({});assert(!loadScene(path,scene,error));assert(scene.materials.size()==1);
    f=std::fopen(path,"wb");assert(f);std::fwrite("FGS2",1,4,f);u32(f,2);u32(f,UINT32_MAX);u32(f,0);u32(f,0);std::fclose(f);
    assert(!loadScene(path,scene,error));std::remove(path);
    WorldScene invalid;invalid.triangles.push_back({0,1,2,0});BVH b;assert(!b.build(std::move(invalid),error));
    std::puts("PASS bounded FGS2 parsing, trailing-data rejection and invalid indices");
}
static void testCrop(){
    WorldScene s;s.materials.push_back({});s.materials.push_back({});
    quad(s,{-1,-1,0},{1,-1,0},{1,1,0},{-1,1,0},1);
    quad(s,{99,-1,0},{101,-1,0},{101,1,0},{99,1,0},0);
    s.completePlacements.push_back({});
    std::string error;assert(cropScene(s,{-2,-2,-2},{2,2,2},error));
    assert(s.completePlacements.empty());
    assert(s.triangles.size()==2&&s.vertices.size()==4&&s.materials.size()==1);
    assert(s.triangles[0].material==0);assert(s.triangles[0].v2<4);
    BVH b=build(std::move(s));assert(b.occluded({0,0,1},{0,0,-1}));assert(!b.occluded({100,0,1},{0,0,-1}));
    std::puts("PASS local scene crop and vertex/material remapping");
}
static void testInstances(){
    std::filesystem::path root=scratchPath("northlight-gi-instance-test");std::filesystem::create_directories(root);
    WorldScene s;s.materials.push_back({});quad(s,{-1,-1,0},{1,-1,0},{1,1,0},{-1,1,0},0);
    for(auto&v:s.vertices)v.normal=normalized({1,1,1});
    std::string model=(root/(std::string(64,'0')+".fgs")).string();writeScene(model.c_str(),s);
    std::string tile=(root/"test.fg3").string();
    auto writeTile=[&](bool singular,bool mixedTerrain=false,bool mixedWmo=false){
        unsigned count=mixedWmo?4:mixedTerrain?3:2;
        FILE*f=std::fopen(tile.c_str(),"wb");assert(f);std::fwrite("FGS3",1,4,f);u32(f,3);u32(f,count);
        for(unsigned instance=0;instance<count;++instance){
            uint8_t hash[32]={};std::fwrite(hash,1,32,f);u32(f,instance+1);u32(f,0);u32(f,instance==2?0:instance==3?2:1);
            float shift=instance==1?100:0;vec(f,{8+shift,19,30});vec(f,{12+shift,21,30});
            const float matrix[9]={0,-2,0,1,0,0,0,0,3};for(auto a:matrix)f32(f,singular?0:a);
            vec(f,{10+shift,20,30});
        }
        std::fclose(f);
    };
    writeTile(false);WorldScene loaded;std::string error;
    assert(loadInstancedScenes({tile,tile},root.string(),{7,18,29},{13,22,31},loaded,error));
    assert(loaded.triangles.size()==2&&loaded.vertices.size()==4&&loaded.materials.size()==1);
    assert(!loaded.materials[0].terrain);
    assert(loaded.completePlacements.size()==1&&loaded.completePlacements[0].uid==1&&loaded.completePlacements[0].category==1);
    assert(loaded.completePlacements[0].modelKey==std::string(64,'0')&&loaded.completePlacements[0].matrix[1]==-2&&loaded.completePlacements[0].translation.x==10);
    assert(near(loaded.vertices[0].position.x,12)&&near(loaded.vertices[0].position.y,19)&&near(loaded.vertices[0].position.z,30));
    assert(dot(loaded.vertices[0].normal,normalized({-.5f,1,1.f/3}))>.9999f);
    BVH b=build(loaded);assert(b.occluded({10,20,35},{0,0,-1},10));
    writeTile(true);assert(!loadInstancedScenes({tile},root.string(),{7,18,29},{13,22,31},loaded,error));assert(loaded.triangles.size()==2);
    writeTile(false,true);assert(loadInstancedScenes({tile,tile},root.string(),{7,18,29},{13,22,31},loaded,error));
    assert(loaded.triangles.size()==4&&loaded.materials.size()==2);
    unsigned terrainTriangles=0;for(const auto&t:loaded.triangles)terrainTriangles+=loaded.materials[t.material].terrain;
    assert(terrainTriangles==2);assert(loaded.materials[0].terrain!=loaded.materials[1].terrain);
    assert(loadInstancedScenes({tile,tile},root.string(),{7,18,29},{13,22,31},loaded,error,1));
    assert(loaded.triangles.size()==2&&loaded.materials.size()==1&&loaded.materials[0].terrain);
    assert(loadInstancedScenes({tile,tile},root.string(),{7,18,29},{13,22,31},loaded,error,2));
    assert(loaded.triangles.size()==2&&!loaded.materials[0].terrain);
    assert(loadInstancedScenes({tile},root.string(),{7,18,29},{13,22,31},loaded,error,0)&&loaded.triangles.empty());
    writeTile(false,true,true);
    assert(loadInstancedScenes({tile,tile},root.string(),{7,18,29},{13,22,31},loaded,error));
    assert(loaded.triangles.size()==6&&loaded.materials.size()==3);
    unsigned wmoTriangles=0,m2Triangles=0;terrainTriangles=0;
    for(const auto& t:loaded.triangles){const auto& m=loaded.materials[t.material];terrainTriangles+=m.terrain;wmoTriangles+=m.wmo;m2Triangles+=!m.terrain&&!m.wmo;assert(!(m.terrain&&m.wmo));}
    assert(wmoTriangles==2&&terrainTriangles==2&&m2Triangles==2);
    assert(loaded.completePlacements.size()==2); // terrain ownership uses existing chunk policy
    WorldScene partial=s;quad(partial,{20,20,0},{21,20,0},{21,21,0},{20,21,0},0);writeScene(model.c_str(),partial);
    writeTile(false);assert(loadInstancedScenes({tile},root.string(),{7,18,29},{13,22,31},loaded,error));
    assert(loaded.triangles.size()==2&&loaded.completePlacements.empty()); // inaccurate descriptor cannot prove a cropped model complete
    writeScene(model.c_str(),s);
    assert(loadScene(model.c_str(),loaded,error));assert(!loaded.materials[0].terrain);
    std::filesystem::remove(tile);std::filesystem::remove(model);std::filesystem::remove(root);
    std::puts("PASS FGS3 transform, inverse-transpose normals, shared models, instance cull/dedup, terrain metadata separation and failure preservation");
}
static void testRealScene(const char* path){
    using Clock=std::chrono::steady_clock;
    auto start=Clock::now();WorldScene s;std::string error;
    if(!loadScene(path,s,error)){std::fprintf(stderr,"Scene load failed: %s\n",error.c_str());assert(false);}
    assert(!s.vertices.empty());size_t original=s.triangles.size();
    Vec3 low=s.vertices[0].position,high=low;
    for(const auto& v:s.vertices){low.x=std::min(low.x,v.position.x);low.y=std::min(low.y,v.position.y);low.z=std::min(low.z,v.position.z);
        high.x=std::max(high.x,v.position.x);high.y=std::max(high.y,v.position.y);high.z=std::max(high.z,v.position.z);}
    Vec3 center=(low+high)*.5f;
    assert(cropScene(s,center-Vec3(128,128,200),center+Vec3(128,128,200),error));
    BVH b;assert(b.build(std::move(s),error));auto built=Clock::now();
    auto floor=b.trace({center.x,center.y,high.z+10},{0,0,-1},.001f,1000);
    if(floor.hit)center.z=floor.position.z+8;
    Lighting light;light.maxDistance=200;double energy=0;unsigned valid=0;
    for(unsigned z=0;z<8;++z)for(unsigned y=0;y<8;++y)for(unsigned x=0;x<8;++x){
        Vec3 p=center+Vec3((float(x)-3.5f)*8,(float(y)-3.5f)*8,(float(z)-3.5f)*8);
        Probe probe=solveProbe(b,p,light,32,x+y*8+z*64+1);valid+=probe.valid;
        Vec3 e=evaluateIrradiance(probe,{0,0,1});assert(std::isfinite(e.x)&&std::isfinite(e.y)&&std::isfinite(e.z));energy+=e.x+e.y+e.z;
    }
    auto done=Clock::now();assert(valid>0);assert(energy>0);
    std::printf("PASS actual FGS2: %zu original triangles, %zu local triangles, center=(%.2f %.2f %.2f), %u/512 valid probes x32 rays: load/crop/BVH %.3fs, solve %.3fs, aggregate irradiance %.3f\n",
        original,b.triangleCount(),center.x,center.y,center.z,valid,std::chrono::duration<double>(built-start).count(),std::chrono::duration<double>(done-built).count(),energy);
}
static void testRealInstanced(const char* tile,const char* modelDirectory,const char* reference){
    using Clock=std::chrono::steady_clock;WorldScene flat,instanced;std::string error;
    assert(loadScene(reference,flat,error));assert(!flat.vertices.empty());
    Vec3 low=flat.vertices[0].position,high=low;
    for(const auto&v:flat.vertices){low.x=std::min(low.x,v.position.x);low.y=std::min(low.y,v.position.y);low.z=std::min(low.z,v.position.z);
        high.x=std::max(high.x,v.position.x);high.y=std::max(high.y,v.position.y);high.z=std::max(high.z,v.position.z);}
    Vec3 c=(low+high)*.5f;low=c-Vec3(128,128,200);high=c+Vec3(128,128,200);
    assert(cropScene(flat,low,high,error));auto start=Clock::now();
    if(!loadInstancedScenes({tile},modelDirectory,low,high,instanced,error)){std::fprintf(stderr,"Instance error: %s\n",error.c_str());assert(false);}
    std::printf("FGS3/FGS2 region triangle counts: %zu / %zu; %.3fs instance load\n",instanced.triangles.size(),flat.triangles.size(),std::chrono::duration<double>(Clock::now()-start).count());
    assert(instanced.triangles.size()==flat.triangles.size());
    BVH a=build(std::move(flat)),b=build(std::move(instanced));unsigned hits=0;
    for(unsigned y=0;y<32;++y)for(unsigned x=0;x<32;++x){Vec3 p={c.x+(float(x)-15.5f)*8,c.y+(float(y)-15.5f)*8,high.z+1};
        auto ah=a.trace(p,{0,0,-1},.001f,1000),bh=b.trace(p,{0,0,-1},.001f,1000);assert(ah.hit==bh.hit);
        if(ah.hit){++hits;assert(near(ah.distance,bh.distance,.02f));assert(dot(ah.normal,bh.normal)>.99f);}}
    assert(hits>100);std::puts("PASS actual FGS3 and FGS2 independently exported geometry agree on 1024 visibility rays");
}
static void testLocalAndMoving(){
    WorldScene floor;WorldMaterial white;white.albedo={1,1,1};floor.materials.push_back(white);
    quad(floor,{-100,-100,0},{100,-100,0},{100,100,0},{-100,100,0},0);
    auto staticWorld=build(floor);Lighting light;light.sunIrradiance={0,0,0};light.skyRadiance={0,0,0};light.maxBounces=1;light.maxDistance=30;
    light.points.push_back({{0,0,10},{10,0,0},12,20});
    auto illuminated=solveProbe(staticWorld,{0,0,1},light,4096,7);
    auto red=evaluateIrradiance(illuminated,{0,0,-1});assert(red.x>1&&red.y==0&&red.z==0);
    WorldScene roof;WorldMaterial black;black.albedo={0,0,0};roof.materials.push_back(black);
    quad(roof,{-100,-100,5},{-100,100,5},{100,100,5},{100,-100,5},0);
    auto actor=build(roof);light.movingGeometry=&actor;
    auto occluded=solveProbe(staticWorld,{0,0,1},light,4096,7);
    assert(actor.occluded({0,0,.01f},{0,0,1},9.99f));
    assert(evaluateIrradiance(occluded,{0,0,-1}).x<red.x*.01f);
    for(auto& v:roof.vertices)v.position.x+=300;auto moved=build(roof);light.movingGeometry=&moved;
    auto restored=solveProbe(staticWorld,{0,0,1},light,4096,7);
    assert(near(evaluateIrradiance(restored,{0,0,-1}).x,red.x,.0001f));
    light.movingGeometry=nullptr;light.points.clear();light.additionalDirections.push_back({{0,0,1},{0,2,0}});
    auto moon=solveProbe(staticWorld,{0,0,1},light,4096,7);auto green=evaluateIrradiance(moon,{0,0,-1});assert(green.y>1.8f&&green.x==0);
    light.points.push_back({{0,0,10},{10,0,0},20,10});assert(!solveProbe(staticWorld,{0,0,1},light,64,7).valid);
    std::puts("PASS authored point-light bounce, moving geometry shadow/removal, secondary celestial light, invalid attenuation rejection");
}
static void testSizeHint(){
    std::filesystem::path root=scratchPath("northlight-gi-hint-test");std::filesystem::create_directories(root);
    WorldScene s;s.materials.push_back({});quad(s,{-1,-1,0},{1,-1,0},{1,1,0},{-1,1,0},0);
    std::string model=(root/(std::string(64,'0')+".fgs")).string();writeScene(model.c_str(),s);
    std::string tile=(root/"hint.fg3").string();const unsigned count=300;
    FILE*f=std::fopen(tile.c_str(),"wb");assert(f);std::fwrite("FGS3",1,4,f);u32(f,3);u32(f,count);
    for(unsigned i=0;i<count;++i){uint8_t hash[32]={};std::fwrite(hash,1,32,f);u32(f,i+1);u32(f,0);u32(f,0);vec(f,{i*10.f-2,19,29});vec(f,{i*10.f+2,21,31});const float m[9]={1,0,0,0,1,0,0,0,1};for(auto a:m)f32(f,a);vec(f,{i*10.f,20,30});}
    std::fclose(f);
    const Vec3 low{-10,0,0},high{3010,40,60};std::string error;
    // Counts admission requests of at least 1 KiB: the geometry growth steps (per-model/material requests are tiny).
    uint64_t largest=0;unsigned large=0;bool deny=false;
    AllocationAdmission admit=[&](uint64_t bytes){largest=std::max(largest,bytes);large+=bytes>=1024;return !(deny&&bytes>=1024);};
    auto same=[](const WorldScene& a,const WorldScene& b){return a.vertices.size()==b.vertices.size()&&a.triangles.size()==b.triangles.size()&&a.materials.size()==b.materials.size()&&!std::memcmp(a.vertices.data(),b.vertices.data(),a.vertices.size()*sizeof(WorldVertex))&&!std::memcmp(a.triangles.data(),b.triangles.data(),a.triangles.size()*sizeof(WorldTriangle));};
    WorldScene plain;if(!loadInstancedScenes({tile},root.string(),low,high,plain,error,1,admit)){std::fprintf(stderr,"hint: %s\n",error.c_str());assert(false);}
    assert(plain.triangles.size()==2*count&&plain.vertices.size()==4*count);
    const unsigned plainSteps=large;assert(plainSteps>2); // x2 chain: 64,128,...
    SceneSizeHint hint;hint.key=7;WorldScene first;
    large=0;largest=0;assert(loadInstancedScenes({tile},root.string(),low,high,first,error,1,admit,{},{},&hint));
    assert(!hint.hinted&&same(first,plain)&&hint.sized&&hint.vertices==4*count&&hint.triangles==2*count&&large==plainSteps);
    // Same region again: one reserve per array at counts + 1/16 (the 3rd large request is the material list), no growth steps, identical geometry.
    WorldScene again;large=0;largest=0;assert(loadInstancedScenes({tile},root.string(),low,high,again,error,1,admit,{},{},&hint));
    assert(hint.hinted&&same(again,plain)&&large==3); // vertex + triangle hint + the one material reserve
    assert(largest==(4*count+4*count/16)*sizeof(WorldVertex));
    // A hint far below the real need: first overflow grows +1/4, then x2, so large growth steps stay within one of an unhinted build.
    {SceneSizeHint small;small.key=1;small.sized=true;small.sizedKey=1;small.vertices=64;small.triangles=32;small.low=low;small.high=high;
     WorldScene s2;large=0;assert(loadInstancedScenes({tile},root.string(),low,high,s2,error,1,admit,{},{},&small));assert(small.hinted&&same(s2,plain)&&large<=plainSteps+1);}
    // A different key (other map or reach) never uses the hint.
    hint.key=8;WorldScene other;assert(loadInstancedScenes({tile},root.string(),low,high,other,error,1,admit,{},{},&hint));assert(!hint.hinted&&same(other,plain));
    // Non-overlapping (IoU < 1/2) region ignores the hint and records its own counts.
    hint.key=8;const Vec3 lowB{1500,0,0},highB{4500,40,60};WorldScene b,bPlain;
    assert(loadInstancedScenes({tile},root.string(),lowB,highB,b,error,1,admit,{},{},&hint));assert(!hint.hinted&&hint.vertices==b.vertices.size());
    assert(loadInstancedScenes({tile},root.string(),lowB,highB,bPlain,error,1,admit));assert(same(b,bPlain)&&b.triangles.size()<plain.triangles.size());
    // Denied hint: falls back to normal growth under the same admission, without an error; a fully denying admission still fails.
    hint.key=9;assert(loadInstancedScenes({tile},root.string(),low,high,again,error,1,admit,{},{},&hint));hint.key=9;
    deny=true;WorldScene denied;assert(!loadInstancedScenes({tile},root.string(),low,high,denied,error,1,admit,{},{},&hint)&&!hint.hinted);
    deny=false;const auto before=hint.vertices;
    // Hint reserve alone is refused (first >=1 KiB request), later growth is allowed.
    unsigned calls=0;AllocationAdmission denyFirst=[&](uint64_t bytes){if(bytes>=1024&&++calls==1)return false;return true;};
    WorldScene fallback;assert(loadInstancedScenes({tile},root.string(),low,high,fallback,error,1,denyFirst,{},{},&hint));
    assert(!hint.hinted&&error.empty()&&same(fallback,plain)&&hint.vertices==before);
    // Without admission the hint still reserves and results are identical.
    WorldScene noAdmit;assert(loadInstancedScenes({tile},root.string(),low,high,noAdmit,error,1,{},{},{},&hint));assert(hint.hinted&&same(noAdmit,plain));
    std::filesystem::remove(tile);std::filesystem::remove(model);std::filesystem::remove(root);
    std::puts("PASS FGS3 size hint: identical geometry, single reserve on overlap, key/IoU gating, denied hint falls back");
}
int main(int argc,char** argv){testLocalAndMoving();testOcclusion();testAlpha();testSamplerModes();testTracerInvariants();testSun();testBounce();testSkyAndMoments();testProbeValidity();testLoader();testCrop();testInstances();testSizeHint();if(argc>1)testRealScene(argv[1]);if(argc>3)testRealInstanced(argv[2],argv[3],argv[1]);std::puts("All world GI tests passed");}
