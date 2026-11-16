// Read-only diagnostic of the runtime's local world-cache build.
#include "scene_source_cache.h"
#include "shadow_terrain.h"
#include "world_mesh_pages.h"
#include "geometry_memory.h"
#include <cmath>
#include <cstdio>
#include <filesystem>
int main(int argc,char** argv){
    if(argc!=6){std::fprintf(stderr,"usage: probe CACHE MAP X Y Z\n");return 2;}
    using namespace NorthlightGI;namespace fs=std::filesystem;
    const fs::path root=argv[1];const std::string map=argv[2];
    Vec3 center{std::stof(argv[3]),std::stof(argv[4]),std::stof(argv[5])};
    const int tx=int(std::floor((17066.6666667-center.y)/533.3333333)),ty=int(std::floor((17066.6666667-center.x)/533.3333333));
    std::vector<std::string> files,farFiles;
    for(int y=ty-2;y<=ty+2;++y)for(int x=tx-2;x<=tx+2;++x){
        char name[32];std::snprintf(name,sizeof name,"%02d_%02d.fg3",x,y);auto p=root/map/name;
        if(fs::exists(p)){farFiles.push_back(p.string());if(std::abs(x-tx)<=1&&std::abs(y-ty)<=1)files.push_back(p.string());}}
    SceneSourceCache sources;LocalSceneCache cache;BVH bvh;std::string error;
    const bool ok=cache.build(files,(root/"models").string(),map,center-Vec3{288,288,320},center+Vec3{288,288,320},bvh,error,{},true,sources.provider());
    std::printf("tiles=%zu result=%u error=%s reads=%llu triangles=%zu\n",files.size(),unsigned(ok),error.c_str(),(unsigned long long)sources.stats().reads,bvh.scene().triangles.size());
    if(!ok)return 1;
    for(Vec3 direction:{Vec3{0,0,1},Vec3{1,0,1},Vec3{-1,0,1},Vec3{0,1,1},Vec3{0,-1,1}}){
        const auto hit=bvh.trace(center,normalized(direction),.05f,128);
        std::printf("ceilingRay=%.0f,%.0f,%.0f hit=%u distance=%.3f\n",direction.x,direction.y,direction.z,unsigned(hit.hit),hit.distance);}
    WorldScene terrain;const float reach=928;
    if(!loadInstancedScenes(farFiles,(root/"models").string(),center-Vec3{reach,reach,reach},center+Vec3{reach,reach,reach},terrain,error,1,{}, {}, {},nullptr,sources.provider(),true)){
        std::fprintf(stderr,"terrain: %s\n",error.c_str());return 1;}
    NorthlightWorldMesh::WorldMeshUploadPlan plan;NorthlightWorldMeshPages::Plan pages;
    if(!NorthlightShadowTerrain::build(bvh.scene(),terrain,center,plan,error,{},reach,{},true,&bvh)||
       !NorthlightWorldMeshPages::build(plan,pages,error)){
        std::fprintf(stderr,"shadow upload: %s\n",error.c_str());return 1;}
    std::printf("shadowTriangles=%u pages=%zu batches=%zu ready=1\n",plan.triangleCount,pages.pages.size(),pages.batches.size());
    uint64_t logicalBytes=0,largestTexture=0,retainedPixels=0;unsigned aliases=0;
    for(const auto& m:plan.materials){logicalBytes+=m.pixelBytes();largestTexture=std::max(largestTexture,m.pixelBytes());retainedPixels+=m.retainedPixelBytes();aliases+=m.textureOwner!=UINT32_MAX;}
    uint64_t opaquePixels=0;unsigned opaqueImages=0;
    for(const auto& m:plan.materials)if(m.textureOwner==UINT32_MAX&&m.pixelBytes()>4){bool opaque=true;for(size_t i=3;i<m.pixels().size();i+=4)if(m.pixels()[i]!=255){opaque=false;break;}if(opaque){opaquePixels+=m.pixelBytes();++opaqueImages;}}
    std::printf("opaqueCutouts=%u redundantOpaqueMiB=%.3f\n",opaqueImages,opaquePixels/1048576.);
    const auto managed=NorthlightGeometryMemory::uploadBudget(pages.vertexBytes,pages.indexBytes,plan.textureBytes,largestTexture);
    const auto deviceLocal=NorthlightGeometryMemory::uploadBudget(0,0,plan.textureBytes,largestTexture);
    std::printf("materials=%zu aliases=%u logicalTextureMiB=%.3f uniqueTextureMiB=%.3f retainedPixelMiB=%.3f managedAdmissionMiB=%.3f deviceLocalAdmissionMiB=%.3f\n",plan.materials.size(),aliases,logicalBytes/1048576.,plan.textureBytes/1048576.,retainedPixels/1048576.,managed.available/1048576.,deviceLocal.available/1048576.);
    return 0;
}
