#pragma once
#include "world_mesh_plan.h"
#include "mesh_page_key.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <new>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

// CPU worker preparation: bounded managed resources keep first residency from
// implicitly uploading a whole city's vertex/index allocation at once. Pages
// preserve geometry exactly; this is partitioning, never simplification.
namespace NorthlightWorldMeshPages {
constexpr uint32_t PageVertexByteLimit=512u*1024u;
constexpr uint32_t PageIndexByteLimit=512u*1024u;
constexpr uint32_t PageVertexLimit=PageVertexByteLimit/sizeof(NorthlightGI::WorldVertex);
constexpr uint32_t PageIndexLimit=PageIndexByteLimit/sizeof(uint32_t);
static_assert(sizeof(NorthlightGI::WorldVertex)==32,"Paging preserves the full world vertex");
static_assert(sizeof(NorthlightGI::Vec3)==12,"Content anchors hash three packed floats");
// 0.3.216 (issue #66): content-defined page boundaries. After a triangle whose position hash is 0 mod anchorModulus,
// and once the page holds minFillVertices, the page closes (at the end of the batch when the batch fits an empty page,
// so such batches stay whole). An insertion or removal anywhere in the plan then only changes the pages up to the next
// anchor; every later page is byte-identical to the previous build's. The hard limits stay as forced cuts.
constexpr uint32_t MinFillVertices=PageVertexLimit/4;
constexpr uint32_t AnchorModulus=6144;
struct Options {uint32_t anchorModulus=AnchorModulus,minFillVertices=MinFillVertices;}; // anchorModulus 0: sequential paging (tests, measurements)
struct Page {
    std::vector<NorthlightGI::WorldVertex> vertices;
    std::vector<uint32_t> indices;
    NorthlightMeshPageKey::Key key; // 0.3.216: content key of vertices+indices, set by build() on the worker
    // 0.3.216: the GPU already holds exactly this page (its key was resident when the plan was built): vertices/indices
    // were freed on the worker, key and exact byte sizes remain. The render thread must find it in the buffer store.
    bool elided=false;
    size_t vertexCount()const{return key.vertexBytes/sizeof(NorthlightGI::WorldVertex);}
    size_t indexCount()const{return key.indexBytes/sizeof(uint32_t);}
};
struct Batch : NorthlightWorldMesh::Batch {
    uint32_t page=0,minVertex=0,vertexCount=0;
};
struct Plan {
    std::vector<Page> pages;
    std::vector<Batch> batches;
    uint64_t vertexBytes=0,indexBytes=0;
    uint64_t cpuBytes() const {
        uint64_t bytes=sizeof(*this)+pages.capacity()*sizeof(Page)+batches.capacity()*sizeof(Batch);
        for(const auto& p:pages)bytes+=p.vertices.capacity()*sizeof(NorthlightGI::WorldVertex)+p.indices.capacity()*sizeof(uint32_t);
        return bytes;
    }
};

// The input indices/batches must be the immutable packed upload plan. Every
// original batch stays whole if it fits an empty page. Larger batches split
// only at triangle boundaries. One bounded source-index map is reused per page;
// a second bounded set preflights a batch without retaining a scene-sized map.
// Failure never modifies output. Materials and scene ownership remain in input.
// Pages also close at content anchors (see Options) and carry a content key; a batch that fits an empty page
// is never split by an anchor.
inline bool build(const NorthlightWorldMesh::WorldMeshUploadPlan& input,Plan& output,std::string& error,
                  const NorthlightGI::AllocationAdmission& admit = {},const Options& options = {}) {
    auto fail=[&](const char* why){error=why;return false;};
    try {
        if(input.indices.size()!=uint64_t(input.triangleCount)*3)
            return fail("Paged mesh triangle/index count mismatch");
        if(!input.indices.empty()&&(!input.source||input.source->vertices.size()!=input.vertexCount))
            return fail("Paged mesh source/vertex count mismatch");
        Plan next;
        if(input.indices.empty()){
            if(!input.batches.empty())return fail("Paged mesh empty indices with batches");
            output=std::move(next);error.clear();return true;
        }
        auto allowed=[&](uint64_t bytes){return !admit||admit(bytes);};
        // Reserve admission for both bounded hash working sets, including nodes.
        if(!allowed(2u*1024u*1024u))return fail("Paged mesh working memory deferred");
        std::unordered_map<uint32_t,uint32_t> remap;
        std::unordered_set<uint32_t> batchUnique;
        remap.reserve(PageVertexLimit);batchUnique.reserve(PageVertexLimit);
        uint64_t expectedStart=0;
        bool closeAfter=false; // an anchor closed the current page: the next triangle/batch opens a new one
        auto newPage=[&](){
            closeAfter=false;
            // Covers both bounded buffers plus vector growth transient copies.
            if(!allowed(2u*(PageVertexByteLimit+PageIndexByteLimit)))return false;
            if(next.pages.size()==next.pages.capacity()) {
                const size_t capacity=std::max<size_t>(1,next.pages.capacity()*2);
                if(!allowed(capacity*sizeof(Page)))return false;
                next.pages.reserve(capacity);
            }
            next.pages.emplace_back();
            // Vertices fill ~95% of a page on real data, so doubling would end at the limit anyway: reserve it once
            // (no realloc chain). Indices fill ~40-55% and keep growing on demand; a full reserve costs ~20% CPU.
            next.pages.back().vertices.reserve(PageVertexLimit);
            remap.clear();return true;
        };
        for(const auto& original:input.batches) {
            const uint64_t count=uint64_t(original.count)*3;
            if(!original.count||original.start!=expectedStart||count>input.indices.size()-expectedStart||original.material>=input.materials.size())
                return fail("Paged mesh malformed batch range/material");
            expectedStart+=count;
            // Preflight only batches that could fit; the set never exceeds one
            // page plus one entry even for an arbitrarily large original batch.
            bool fitsEmpty=count<=PageIndexLimit;
            uint32_t missing=0;
            batchUnique.clear();
            if(fitsEmpty) {
                for(uint64_t i=original.start;i<expectedStart;++i) {
                    const auto sourceIndex=input.indices[size_t(i)];
                    if(sourceIndex>=input.vertexCount)return fail("Paged mesh index outside source vertices");
                    if(batchUnique.insert(sourceIndex).second) {
                        if(batchUnique.size()>PageVertexLimit){fitsEmpty=false;break;}
                        if(remap.find(sourceIndex)==remap.end())++missing;
                    }
                }
            }
            bool anchored=false; // anchor hit inside a batch that must stay whole: the page closes after the batch
            if(next.pages.empty()||closeAfter||(fitsEmpty&&(next.pages.back().indices.size()+count>PageIndexLimit||next.pages.back().vertices.size()+missing>PageVertexLimit)))
                if(!newPage())return fail("Paged mesh page allocation deferred");
            size_t currentBatch=std::numeric_limits<size_t>::max();
            for(uint64_t i=original.start;i<expectedStart;i+=3) {
                uint32_t ids[3],newVertices=0;
                for(unsigned k=0;k<3;++k) {
                    ids[k]=input.indices[size_t(i+k)];
                    if(ids[k]>=input.vertexCount)return fail("Paged mesh index outside source vertices");
                    bool repeated=false;for(unsigned previous=0;previous<k;++previous)repeated|=ids[k]==ids[previous];
                    if(!repeated&&remap.find(ids[k])==remap.end())++newVertices;
                }
                if(closeAfter||next.pages.back().vertices.size()+newVertices>PageVertexLimit||next.pages.back().indices.size()+3>PageIndexLimit) {
                    if(!newPage())return fail("Paged mesh page allocation deferred");
                    currentBatch=std::numeric_limits<size_t>::max();
                }
                auto& page=next.pages.back();
                if(currentBatch==std::numeric_limits<size_t>::max()) {
                    Batch b;static_cast<NorthlightWorldMesh::Batch&>(b)=original;
                    b.start=uint32_t(page.indices.size());b.count=0;b.page=uint32_t(next.pages.size()-1);
                    const float inf=std::numeric_limits<float>::infinity();
                    b.boundsLow={inf,inf,inf};b.boundsHigh={-inf,-inf,-inf};
                    b.minVertex=std::numeric_limits<uint32_t>::max();b.vertexCount=0;
                    if(next.batches.size()==next.batches.capacity()) {
                        const size_t capacity=std::max<size_t>(1,next.batches.capacity()*2);
                        if(!allowed(capacity*sizeof(Batch)))return fail("Paged mesh batch allocation deferred");
                        next.batches.reserve(capacity);
                    }
                    next.batches.push_back(b);currentBatch=next.batches.size()-1;
                }
                auto& batch=next.batches[currentBatch];
                uint32_t maxVertex=batch.vertexCount?batch.minVertex+batch.vertexCount-1:0;
                for(unsigned k=0;k<3;++k) {
                    auto insertion=remap.emplace(ids[k],uint32_t(page.vertices.size()));
                    if(insertion.second) {
                        const auto& vertex=input.source->vertices[ids[k]];
                        if(!std::isfinite(vertex.position.x)||!std::isfinite(vertex.position.y)||!std::isfinite(vertex.position.z))
                            return fail("Paged mesh nonfinite vertex position");
                        page.vertices.push_back(vertex);
                    }
                    const uint32_t local=insertion.first->second;
                    page.indices.push_back(local);
                    batch.minVertex=std::min(batch.minVertex,local);maxVertex=std::max(maxVertex,local);
                    const auto& p=page.vertices[local].position;
                    batch.boundsLow.x=std::min(batch.boundsLow.x,p.x);batch.boundsLow.y=std::min(batch.boundsLow.y,p.y);batch.boundsLow.z=std::min(batch.boundsLow.z,p.z);
                    batch.boundsHigh.x=std::max(batch.boundsHigh.x,p.x);batch.boundsHigh.y=std::max(batch.boundsHigh.y,p.y);batch.boundsHigh.z=std::max(batch.boundsHigh.z,p.z);
                }
                batch.vertexCount=maxVertex-batch.minVertex+1;++batch.count;
                if(options.anchorModulus&&page.vertices.size()>=options.minFillVertices) {
                    const auto& sv=input.source->vertices;
                    const uint64_t anchor=NorthlightMeshPageKey::triangleHash(&sv[ids[0]].position.x,&sv[ids[1]].position.x,&sv[ids[2]].position.x);
                    if(anchor%options.anchorModulus==0){if(fitsEmpty)anchored=true;else closeAfter=true;}
                }
            }
            if(anchored)closeAfter=true;
        }
        if(expectedStart!=input.indices.size())return fail("Paged mesh batches do not cover all indices");
        for(auto& p:next.pages) {
            p.key=NorthlightMeshPageKey::keyOf(p.vertices.data(),p.vertices.size()*sizeof(NorthlightGI::WorldVertex),p.indices.data(),p.indices.size()*sizeof(uint32_t));
            next.vertexBytes+=p.vertices.size()*sizeof(NorthlightGI::WorldVertex);next.indexBytes+=p.indices.size()*sizeof(uint32_t);
        }
        output=std::move(next);error.clear();return true;
    } catch(const std::bad_alloc&) {return fail("Paged mesh worker allocation failed");}
      catch(const std::length_error&) {return fail("Paged mesh worker allocation size overflow");}
}

// Worker, after build() and before seal(): frees the vertex/index data of every page whose content key the GPU already
// holds (`resident`, published by the render thread from its ACTIVE buffers). The pages keep key, sizes and batches, so
// the plan stays valid and the aggregate sizes stay the full mesh's; cpuBytes() drops by the freed capacity.
struct ElisionStats {size_t pages=0;uint64_t bytes=0;};
inline ElisionStats elide(Plan& plan,const NorthlightMeshPageKey::KeySet& resident) {
    ElisionStats stats;
    if(resident.empty())return stats;
    for(auto& page:plan.pages) {
        if(page.elided||!resident.count(page.key))continue;
        stats.bytes+=page.vertices.capacity()*sizeof(NorthlightGI::WorldVertex)+page.indices.capacity()*sizeof(uint32_t);
        std::vector<NorthlightGI::WorldVertex>().swap(page.vertices);std::vector<uint32_t>().swap(page.indices);
        page.elided=true;++stats.pages;
    }
    return stats;
}

// Cheap render-side validation: individual indices/vertex attributes were
// validated during immutable worker preparation. Never walk millions of them
// again when publishing a new scene. Pages and batches are in append order.
inline const char* validatePages(const NorthlightWorldMesh::WorldMeshUploadPlan& input,const Plan& pages,const std::vector<NorthlightWorldMesh::MaterialUpload>& materials) {
    if(!input.vertexCount||!input.triangleCount||materials.empty()||pages.pages.empty()||pages.batches.empty())return "empty paged upload plan";
    if(input.vertexBytes!=uint64_t(input.vertexCount)*sizeof(NorthlightGI::WorldVertex)||input.vertexBytes>UINT32_MAX||
       input.indexBytes!=uint64_t(input.triangleCount)*3*sizeof(uint32_t)||input.indexBytes>UINT32_MAX)return "paged original buffer sizes mismatch";
    uint64_t vertexBytes=0,indexBytes=0;
    for(const auto& page:pages.pages) {
        // An elided page has no data; its key carries the exact sizes. Every other page must match its key.
        if(page.elided?!page.vertices.empty()||!page.indices.empty():page.key.vertexBytes!=page.vertices.size()*sizeof(NorthlightGI::WorldVertex)||page.key.indexBytes!=page.indices.size()*sizeof(uint32_t))return "mesh page key size mismatch";
        if(!page.vertexCount()||!page.indexCount()||page.key.vertexBytes%sizeof(NorthlightGI::WorldVertex)||page.key.indexBytes%sizeof(uint32_t)||page.vertexCount()>PageVertexLimit||page.indexCount()>PageIndexLimit||page.indexCount()%3)return "invalid mesh page size";
        vertexBytes+=page.key.vertexBytes;indexBytes+=page.key.indexBytes;
    }
    if(vertexBytes!=pages.vertexBytes||indexBytes!=pages.indexBytes||indexBytes!=input.indexBytes)return "paged aggregate sizes mismatch";
    size_t pageIndex=0;uint64_t nextIndex=0;
    for(const auto& batch:pages.batches) {
        if(pageIndex>=pages.pages.size()||batch.page!=pageIndex||batch.start!=nextIndex||!batch.count||batch.material>=materials.size())return "invalid paged batch range/material";
        const auto& page=pages.pages[pageIndex];
        if(!batch.vertexCount||uint64_t(batch.minVertex)+batch.vertexCount>page.vertexCount())return "invalid paged batch vertex range";
        nextIndex+=uint64_t(batch.count)*3;
        if(nextIndex>page.indexCount())return "paged batch exceeds page indices";
        if(nextIndex==page.indexCount()){++pageIndex;nextIndex=0;}
    }
    if(pageIndex!=pages.pages.size()||nextIndex)return "incomplete paged batch coverage";
    for(const auto& material:materials)
        if(!material.width||!material.height||material.width>4096||material.height>4096||uint64_t(material.width)*material.height*4!=material.bgra.size())return "invalid paged texture size";
    return nullptr;
}
inline const char* validatePages(const NorthlightWorldMesh::WorldMeshUploadPlan& input,const Plan& pages) {return validatePages(input,pages,input.materials);}
// Render thread only (shares the payload). A released payload is not a sealed plan any more.
inline const char* validateSealed(const NorthlightWorldMesh::WorldMeshUploadPlan& input) {
    if(!input.pagesSealed||input.source||input.ownedSource||!input.indices.empty())return "paged ownership not sealed";
    const auto payload=input.payload.share();
    if(!payload||!payload->pages)return "paged payload released";
    return validatePages(input,*payload->pages,payload->materials);
}

// Must run on the worker before publication. Copy the small placement proofs
// first, then discard the temporary merged shadow scene and original packed
// indices. No GI scene is modified, including for a non-owning original plan.
// All potentially failing work precedes mutation of input.
inline bool seal(NorthlightWorldMesh::WorldMeshUploadPlan& input,std::shared_ptr<const Plan> pages,std::string& error,
                 const NorthlightGI::AllocationAdmission& admit = {}) {
    auto fail=[&](const char* reason){error=reason;return false;};
    if(input.pagesSealed||!input.source||!pages)return fail("paged seal requires unsealed source and pages");
    if(input.ownedSource&&input.source!=input.ownedSource.get())return fail("paged seal source identity mismatch");
    if(input.source->vertices.size()!=input.vertexCount||input.source->triangles.size()!=input.triangleCount||input.source->materials.size()!=input.materials.size()||input.indices.size()!=uint64_t(input.triangleCount)*3)return fail("paged seal source counts mismatch");
    if(const char* reason=validatePages(input,*pages))return fail(reason);
    try {
        uint64_t ownerBytes=input.source->completePlacements.size()*sizeof(NorthlightGI::WorldPlacementCoverage);
        for(const auto& owner:input.source->completePlacements)ownerBytes+=owner.modelKey.size()+1;
        if(admit&&!admit(ownerBytes))return fail("paged ownership allocation deferred");
        auto owners=input.source->completePlacements;
        const uint64_t cpuBytes=pages->cpuBytes();
        // 0.3.214 (task 42): pages and cutout textures move into the separately owned payload.
        auto payload=std::make_shared<NorthlightWorldMesh::UploadPayload>();
        payload->bytes=cpuBytes+sizeof(NorthlightWorldMesh::UploadPayload)+input.materials.capacity()*sizeof(NorthlightWorldMesh::MaterialUpload)+64;
        for(const auto& material:input.materials)payload->bytes+=material.bgra.capacity();
        payload->pages=std::move(pages);payload->materials.swap(input.materials);
        input.completePlacements=std::move(owners);input.payload.set(std::move(payload));input.pagedCpuBytes=cpuBytes;
        input.source=nullptr;input.ownedSource.reset();std::vector<uint32_t>().swap(input.indices);
        input.pagesSealed=true;error.clear();return true;
    } catch(const std::bad_alloc&) {return fail("paged ownership allocation failed");}
      catch(const std::length_error&) {return fail("paged ownership size overflow");}
}
} // namespace NorthlightWorldMeshPages
