#include "shadow_terrain.h"
#include "world_mesh_pages.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <filesystem>
using namespace NorthlightGI;
int main(int argc,char**argv){
    assert(argc==2);const std::string root=argv[1];const Vec3 center(5872,672,650);
    const int tx=int(std::floor((17066.6666667-center.y)/533.3333333)),ty=int(std::floor((17066.6666667-center.x)/533.3333333));
    std::vector<std::string> nearFiles,farFiles;
    for(int y=ty-2;y<=ty+2;++y)for(int x=tx-2;x<=tx+2;++x){
        auto p=root+"/Northrend/"+std::to_string(x)+"_"+std::to_string(y)+".fg3";
        if(std::filesystem::exists(p)){farFiles.push_back(p);if(std::abs(x-tx)<=1&&std::abs(y-ty)<=1)nearFiles.push_back(p);}
    }
    WorldScene local,far;std::string error;
    assert(loadInstancedScenes(nearFiles,root+"/models",center-Vec3(288,288,320),center+Vec3(288,288,320),local,error));
    const auto radius=NorthlightShadowTerrain::Radius;
    assert(loadInstancedScenes(farFiles,root+"/models",center-Vec3(radius,radius,radius),center+Vec3(radius,radius,radius),far,error,1));
    NorthlightWorldMesh::WorldMeshUploadPlan legacy,compact;
    assert(NorthlightShadowTerrain::build(local,far,center,legacy,error));
    assert(NorthlightShadowTerrain::build(local,far,center,compact,error,{},radius,{},true));
    assert(!compact.source&&!compact.ownedSource&&compact.pagingVertices);
    assert(legacy.indices==compact.indices&&legacy.fixedTerrainChunks==compact.fixedTerrainChunks);
    assert(legacy.vertexCount==compact.vertexCount&&legacy.triangleCount==compact.triangleCount);
    assert(legacy.materials.size()==compact.materials.size());
    for(size_t i=0;i<legacy.materials.size();++i){
        const auto&a=legacy.materials[i];const auto&b=compact.materials[i];
        assert(a.width==b.width&&a.height==b.height&&a.bgra==b.bgra&&a.alphaCutoff==b.alphaCutoff&&a.terrain==b.terrain);
    }
    assert(!std::memcmp(legacy.source->vertices.data(),compact.pagingVertices->data(),size_t(legacy.vertexBytes)));
    const uint64_t saved=legacy.cpuBytes()-compact.cpuBytes();
    assert(saved>=legacy.source->triangles.capacity()*sizeof(WorldTriangle));
    const auto triangles=compact.triangleCount;
    namespace P=NorthlightWorldMeshPages;
    P::Plan reference;assert(P::build(legacy,reference,error));
    auto pages=std::make_shared<P::Plan>();assert(P::build(compact,*pages,error));
    assert(reference.pages.size()==pages->pages.size()&&reference.batches.size()==pages->batches.size());
    for(size_t i=0;i<reference.pages.size();++i){
        const auto&a=reference.pages[i];const auto&b=pages->pages[i];
        assert(a.key==b.key&&a.indices==b.indices&&a.vertices.size()==b.vertices.size());
        assert(!std::memcmp(a.vertices.data(),b.vertices.data(),a.vertices.size()*sizeof(WorldVertex)));
    }
    assert(P::seal(compact,pages,error)&&!P::validateSealed(compact));
    assert(local.triangles.size()>1000000&&saved>30u*1024u*1024u);
    std::printf("PASS Dalaran localTriangles=%zu shadowTriangles=%u pages=%zu freedBeforePagingMiB=%.3f exactGeometry=1 exactAlpha=1 reserveChanged=0\n",
        local.triangles.size(),triangles,pages->pages.size(),double(saved)/1048576);
}
