#include "lamp_fixture_metadata.h"
#include "world_mesh_plan.h"
#include "world_mesh_pages.h"
#include "static_shadow_scene.h"
#include <cassert>
#include <cstdio>
#include <algorithm>
#include <filesystem>
using namespace NorthlightGI;
static void check(const WorldScene& scene,const NorthlightLocalLights::Light& light){
    NorthlightWorldMesh::WorldMeshUploadPlan plan;std::string error;
    assert(NorthlightWorldMesh::buildUploadPlan(scene,plan,error));
    const auto own=LampFixtureMetadata::parts(light,plan.batches);assert(!own.empty());
    auto foreign=light;foreign.sourceId^=1;assert(LampFixtureMetadata::parts(foreign,plan.batches).empty());
    NorthlightWorldMeshPages::Plan pages;assert(NorthlightWorldMeshPages::build(plan,pages,error));
    const auto page=LampFixtureMetadata::parts(light,pages.batches);assert(page.size()==own.size());
    for(size_t i=0;i<own.size();++i)assert(pages.batches[page[i]].count==plan.batches[own[i]].count);
    size_t frame=0;for(const auto& b:plan.batches)if(!b.lampSourceId)frame+=b.count;assert(frame>0);
}
int main(int argc,char** argv){
    assert(argc==2);std::string root=argv[1],error;
    std::vector<NorthlightLocalLights::Light> lights;assert(NorthlightLocalLights::readFile(root+"/sources.fgl",lights)&&lights.size()==1);
    const Vec3 low{-100000,-100000,-100000},high{100000,100000,100000};
    WorldScene scene;assert(loadInstancedScenes({root+"/tile.fg3"},root+"/models",low,high,scene,error));check(scene,lights[0]);
    // The independent directional loader must decode the tag while retaining
    // the complete opaque enclosure for sun, moon and other light sources.
    std::filesystem::create_directories(root+"/Lamp");
    std::filesystem::copy_file(root+"/tile.fg3",root+"/Lamp/30_48.fg3",std::filesystem::copy_options::overwrite_existing);
    {StaticShadow::Streamer stream(root);StaticShadow::Request request;request.map="Lamp";request.center={lights[0].position[0],lights[0].position[1],lights[0].position[2]};
     stream.request(request);assert(stream.waitIdle());auto snapshot=stream.snapshot();assert(snapshot&&snapshot->models.size()==1);
     const auto& model=*snapshot->models.begin()->second;assert(model.indexCount()==scene.triangles.size()*3);
     for(const auto& material:model.materials)assert(material.alphaCutoff>=0.f);}
    LocalSceneCache cache;BVH incremental;assert(cache.build({root+"/tile.fg3"},root+"/models","Azeroth",low,high,incremental,error));check(incremental.scene(),lights[0]);
    BVH reused;assert(cache.build({root+"/tile.fg3"},root+"/models","Azeroth",low,high,reused,error));check(reused.scene(),lights[0]);
    WorldScene untagged=scene;for(auto& material:untagged.materials)material.lampSourceId=0;
    BVH full,plain;assert(full.build(std::move(scene),error,true)&&plain.build(std::move(untagged),error,true));
    const auto& l=lights[0];const Vec3 origin{l.position[0],l.position[1],l.position[2]};unsigned blocked=0;
    for(unsigned i=0;i<4096;++i){const float z=1.f-2.f*(i+.5f)/4096,r=std::sqrt(1-z*z),phi=i*2.39996323f;const Vec3 dir{r*std::cos(phi),r*std::sin(phi),z};
        const auto a=full.trace(origin,dir,.01f,8.f),b=plain.trace(origin,dir,.01f,8.f);
        assert(a.hit==b.hit);if(a.hit){assert(a.distance==b.distance);++blocked;}
    }
    assert(blocked>100);
    std::printf("M2_LAMP_PASS %s rays=4096 blocked=%u taggedUntagged=exact\n",argv[1],blocked);
}
