#include "world_mesh_pages.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <limits>

using namespace NorthlightGI;
namespace P=NorthlightWorldMeshPages;
using NorthlightWorldMesh::WorldMeshUploadPlan;
static WorldVertex vertex(uint32_t i) {
    return {{float(i%123)-50.f,float(i%327)-100.f,float(i%91)-20.f},
            {float(i%11)*.125f,float(i%17)*-.0625f,float(i%7)*.25f},
            float(i%113)*.03125f,float(i%37)*-.125f};
}
static void initialize(WorldScene& source,WorldMeshUploadPlan& input,uint32_t vertices) {
    for(uint32_t i=0;i<vertices;++i)source.vertices.push_back(vertex(i));
    input.source=&source;input.vertexCount=vertices;input.materials.resize(3);
}
static void addBatch(WorldMeshUploadPlan& input,uint32_t triangles,uint32_t first,uint32_t span,int material=0) {
    NorthlightWorldMesh::Batch b;b.start=uint32_t(input.indices.size());b.count=triangles;b.material=material;
    b.terrain=material==1;b.chunkX=b.terrain?132:-1;b.chunkY=b.terrain?321:-1;
    for(uint32_t i=0;i<triangles*3;++i)input.indices.push_back(first+i%span);
    input.batches.push_back(b);input.triangleCount+=triangles;
}
static void verify(const WorldMeshUploadPlan& input,const P::Plan& output) {
    size_t originalBatch=0,originalTriangle=0,total=0;
    uint64_t vb=0,ib=0;
    for(const auto& page:output.pages){
        assert(!page.vertices.empty()&&!page.indices.empty());
        assert(page.vertices.size()*sizeof(WorldVertex)<=P::PageVertexByteLimit);
        assert(page.indices.size()*sizeof(uint32_t)<=P::PageIndexByteLimit);
        vb+=page.vertices.size()*sizeof(WorldVertex);ib+=page.indices.size()*sizeof(uint32_t);
    }
    std::vector<size_t> pageNext(output.pages.size());
    for(const auto& b:output.batches){
        assert(originalBatch<input.batches.size());const auto& before=input.batches[originalBatch];
        assert(b.page<output.pages.size());const auto& page=output.pages[b.page];
        assert(b.start==pageNext[b.page]);pageNext[b.page]+=size_t(b.count)*3;
        assert(b.count&&b.start+size_t(b.count)*3<=page.indices.size());
        assert(b.material==before.material&&b.terrain==before.terrain&&b.chunkX==before.chunkX&&b.chunkY==before.chunkY);
        assert(originalTriangle+b.count<=before.count);
        const float inf=std::numeric_limits<float>::infinity();Vec3 lo(inf,inf,inf),hi(-inf,-inf,-inf);
        uint32_t min=UINT32_MAX,max=0;
        for(uint32_t t=0;t<b.count;++t){
            for(unsigned k=0;k<3;++k){
                uint32_t originalIndex=input.indices[before.start+(originalTriangle+t)*3+k];
                uint32_t actualIndex=page.indices[b.start+t*3+k];assert(actualIndex<page.vertices.size());
                assert(std::memcmp(&input.source->vertices[originalIndex],&page.vertices[actualIndex],sizeof(WorldVertex))==0);
                const auto& v=page.vertices[actualIndex].position;
                lo.x=std::min(lo.x,v.x);lo.y=std::min(lo.y,v.y);lo.z=std::min(lo.z,v.z);
                hi.x=std::max(hi.x,v.x);hi.y=std::max(hi.y,v.y);hi.z=std::max(hi.z,v.z);
                min=std::min(min,actualIndex);max=std::max(max,actualIndex);
            }
        }
        assert(b.minVertex==min&&b.vertexCount==max-min+1);
        assert(b.boundsLow.x==lo.x&&b.boundsLow.y==lo.y&&b.boundsLow.z==lo.z);
        assert(b.boundsHigh.x==hi.x&&b.boundsHigh.y==hi.y&&b.boundsHigh.z==hi.z);
        total+=b.count;originalTriangle+=b.count;
        if(originalTriangle==before.count){++originalBatch;originalTriangle=0;}
    }
    for(size_t i=0;i<output.pages.size();++i)assert(pageNext[i]==output.pages[i].indices.size());
    assert(total==input.triangleCount&&originalBatch==input.batches.size());
    assert(output.vertexBytes==vb&&output.indexBytes==ib&&output.cpuBytes()>=vb+ib);
}
static void testVertexLimits(){
    WorldScene scene;WorldMeshUploadPlan input;initialize(scene,input,P::PageVertexLimit*4);
    addBatch(input,30000,0,P::PageVertexLimit*4,1);
    P::Plan plan;std::string error;assert(P::build(input,plan,error));verify(input,plan);
    assert(plan.pages.size()>4);assert(plan.batches.size()==plan.pages.size());
    // Shared vertices are copied at page boundaries, but never lose attributes.
    assert(plan.vertexBytes>=scene.vertices.size()*sizeof(WorldVertex));
    std::puts("PASS huge batch: bounded vertex pages, exact attributes/order/bounds");
}
static void testIndexLimits(){
    WorldScene scene;WorldMeshUploadPlan input;initialize(scene,input,3);
    addBatch(input,100000,0,3,2);
    P::Plan plan;std::string error;assert(P::build(input,plan,error));verify(input,plan);
    assert(plan.pages.size()==3);assert(plan.pages[0].indices.size()==(P::PageIndexLimit/3)*3);
    assert(plan.pages[0].vertices.size()==3);
    std::puts("PASS reused vertices: index limit and page-local index remapping");
}
static void testKeepBatches(){
    WorldScene scene;WorldMeshUploadPlan input;initialize(scene,input,20000);
    addBatch(input,3000,0,9000,0);addBatch(input,3000,10000,9000,1);
    addBatch(input,1000,10000,3000,2);
    P::Plan plan;std::string error;assert(P::build(input,plan,error));verify(input,plan);
    assert(plan.batches.size()==3&&plan.pages.size()==2);
    assert(plan.batches[0].page==0&&plan.batches[1].page==1&&plan.batches[2].page==1);
    assert(plan.pages[1].vertices.size()==9000); // Shared across batches.
    std::puts("PASS fitting batches stay whole; material/chunk boundaries and cross-batch sharing preserved");
}
static void testDegenerateAndExactBoundary(){
    WorldScene scene;WorldMeshUploadPlan input;initialize(scene,input,P::PageVertexLimit);
    addBatch(input,P::PageVertexLimit/3,0,P::PageVertexLimit,1);
    addBatch(input,1,P::PageVertexLimit-1,1,0); // All three indices are identical.
    addBatch(input,1,0,1,2);
    P::Plan plan;std::string error;assert(P::build(input,plan,error));verify(input,plan);
    assert(plan.pages.size()==1&&plan.pages[0].vertices.size()==P::PageVertexLimit);
    std::puts("PASS exact vertex-capacity boundary and repeated indices within a triangle");
}
static void testTransactionalFailure(){
    WorldScene scene;WorldMeshUploadPlan input;initialize(scene,input,3);addBatch(input,1,0,3);
    P::Plan plan;std::string error;assert(P::build(input,plan,error));
    const auto* priorVertices=plan.pages[0].vertices.data();const auto bytes=plan.cpuBytes();
    auto bad=[&](WorldMeshUploadPlan broken){assert(!P::build(broken,plan,error));assert(!error.empty());assert(plan.pages[0].vertices.data()==priorVertices&&plan.cpuBytes()==bytes);verify(input,plan);};
    assert(!P::build(input,plan,error,[](uint64_t){return false;}));
    assert(plan.pages[0].vertices.data()==priorVertices);
    for(unsigned rejectAt=1;rejectAt<=4;++rejectAt){unsigned admissions=0;
        assert(!P::build(input,plan,error,[&](uint64_t){return ++admissions!=rejectAt;}));
        assert(plan.pages[0].vertices.data()==priorVertices);verify(input,plan);
    }
    auto broken=input;broken.indices[2]=3;bad(broken);
    broken=input;broken.batches[0].start=UINT32_MAX;bad(broken);
    broken=input;broken.batches[0].count=UINT32_MAX;bad(broken);
    broken=input;broken.batches[0].material=3;bad(broken);
    broken=input;broken.batches.clear();bad(broken);
    broken=input;broken.batches[0].count=0;bad(broken);
    broken=input;broken.triangleCount=0;bad(broken);
    broken=input;broken.source=nullptr;bad(broken);
    scene.vertices[1].position.x=std::numeric_limits<float>::quiet_NaN();
    assert(!P::build(input,plan,error));assert(plan.pages[0].vertices.data()==priorVertices);
    scene.vertices[1]=vertex(1);
    WorldMeshUploadPlan empty;assert(P::build(empty,plan,error));verify(empty,plan);assert(plan.pages.empty()&&plan.batches.empty());
    std::puts("PASS malformed plan/index rejection, transactional failure and empty plan");
}
static void testSeal(){
    auto source=std::make_shared<WorldScene>();WorldMeshUploadPlan input;initialize(*source,input,3);addBatch(input,1,0,3);
    source->triangles.push_back({0,1,2,0});source->materials.resize(3);
    WorldPlacementCoverage owner;owner.uid=451;owner.modelKey="exact-static-owner";owner.matrix[0]=1;owner.translation={1,2,3};source->completePlacements.push_back(owner);
    input.ownedSource=source;input.vertexBytes=3*sizeof(WorldVertex);input.indexBytes=3*sizeof(uint32_t);
    for(auto& material:input.materials)material.bgra={255,255,255,255};
    std::string error;auto pages=std::make_shared<P::Plan>();assert(P::build(input,*pages,error));
    assert(!P::validatePages(input,*pages));
    assert(!P::seal(input,pages,error,[](uint64_t){return false;}));
    assert(input.source==source.get()&&!input.pagesSealed&&!input.indices.empty()&&!input.payload.live());
    auto broken=*pages;broken.batches[0].page=1;assert(P::validatePages(input,broken));
    broken=*pages;broken.batches[0].vertexCount=4;assert(P::validatePages(input,broken));
    broken=*pages;broken.batches[0].count=2;assert(P::validatePages(input,broken));
    broken=*pages;broken.indexBytes++;assert(P::validatePages(input,broken));
    broken=*pages;broken.pages[0].indices.push_back(0);assert(P::validatePages(input,broken));
    assert(!P::seal(input,std::make_shared<P::Plan>(broken),error));assert(input.source==source.get());
    assert(P::seal(input,pages,error));assert(!P::validateSealed(input));
    assert(input.source==nullptr&&!input.ownedSource&&input.indices.capacity()==0&&input.payload.share()->pages==pages&&input.materials.capacity()==0&&input.payload.live());
    assert(input.completePlacements.size()==1&&input.completePlacements[0].uid==owner.uid&&input.completePlacements[0].modelKey==owner.modelKey);
    assert(source->vertices.size()==3&&source->triangles.size()==1&&source->completePlacements[0].modelKey==owner.modelKey);
    assert(input.pagedCpuBytes==pages->cpuBytes());
    assert(!P::seal(input,pages,error));assert(!P::validateSealed(input));
    input.source=source.get();assert(P::validateSealed(input));input.source=nullptr;
    // Non-owning plans can seal without mutating/releasing their GI scene.
    WorldMeshUploadPlan borrowed;initialize(*source,borrowed,0);borrowed.vertexCount=3;borrowed.source=source.get();addBatch(borrowed,1,0,3);
    borrowed.vertexBytes=96;borrowed.indexBytes=12;for(auto& material:borrowed.materials)material.bgra={255,255,255,255};
    assert(P::seal(borrowed,pages,error));assert(!P::validateSealed(borrowed));assert(source->vertices.size()==3);
    std::puts("PASS sealed ownership: source release, exact placement proofs, retained GI, cheap validation and transactional failures");
}
int main(){testVertexLimits();testIndexLimits();testKeepBatches();testDegenerateAndExactBoundary();testTransactionalFailure();testSeal();std::puts("All world mesh page tests passed");}
