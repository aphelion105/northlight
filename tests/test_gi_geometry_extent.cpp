#include "shadow_terrain.h"
#include "quality_settings.h"
#include <cassert>
#include <cstdio>

static void triangle(NorthlightGI::WorldScene& scene,float x){
    using namespace NorthlightGI;
    const auto first=uint32_t(scene.vertices.size());
    for(Vec3 p:{Vec3(x,0,0),Vec3(x+1,0,0),Vec3(x,1,0)}){
        WorldVertex v;v.position=p;v.normal={0,0,1};scene.vertices.push_back(v);
    }
    scene.triangles.push_back({first,first+1,first+2,0});
}
int main(){
    using namespace NorthlightGI;
    for(unsigned rings=1;rings<=3;++rings){
        auto q=NorthlightQuality::Settings{};q.giChunkRadius=rings;
        const float extent=NorthlightQuality::giGeometryRadius(q);
        WorldScene local,extended;WorldMaterial material;material.terrain=true;
        local.materials.push_back(material);extended.materials.push_back(material);
        triangle(local,extent-8);triangle(extended,extent-8);triangle(extended,extent+40);
        for(bool spooled:{false,true}){
            NorthlightWorldMesh::WorldMeshUploadPlan plan;std::string error;
            assert(NorthlightShadowTerrain::build(local,extended,{},plan,error,{},1120,{},true,nullptr,nullptr,nullptr,spooled,extent));
            assert(plan.triangleCount==2); // No duplicated GI terrain in the directional mesh.
        }
        assert(NorthlightQuality::giChunkSide(q)==2*rings+1);
    }
    puts("GI_GEOMETRY_EXTENT_PASS rings=3 flatAndSpooled=1 duplicateTerrain=0");
}
