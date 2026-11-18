#pragma once
#include "world_mesh_plan.h"
#include "mesh_page_key.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <new>
#include <stdexcept>
#include <type_traits>
#include <utility>

// CPU worker preparation: bounded managed resources keep first residency from
// implicitly uploading a whole city's vertex/index allocation at once. Pages
// preserve geometry exactly; this is partitioning, never simplification.
namespace NorthlightWorldMeshPages {
constexpr uint32_t PageVertexByteLimit=512u*1024u;
constexpr uint32_t PageIndexByteLimit=512u*1024u;
constexpr uint32_t PageVertexLimit=PageVertexByteLimit/sizeof(NorthlightGI::WorldVertex);
constexpr uint32_t PageIndexLimit=PageIndexByteLimit/sizeof(uint32_t);
static_assert(sizeof(NorthlightGI::WorldVertex)==32,"Paging preserves the full world vertex");
static_assert(PageVertexLimit<=65536,"Page-local indices must fit in 16 bits");
static_assert(sizeof(NorthlightGI::Vec3)==12,"Content anchors hash three packed floats");
// 0.3.216 (issue #66): content-defined page boundaries. After a triangle whose position hash is 0 mod anchorModulus,
// and once the page holds minFillVertices, the page closes (at the end of the batch when the batch fits an empty page,
// so such batches stay whole). An insertion or removal anywhere in the plan then only changes the pages up to the next
// anchor; every later page is byte-identical to the previous build's. The hard limits stay as forced cuts.
constexpr uint32_t MinFillVertices=PageVertexLimit/4;
constexpr uint32_t AnchorModulus=6144;
// anchorModulus 0: sequential paging (tests, measurements). resident (optional): keys of pages the GPU already holds; a page
// whose key is in it is freed the moment it is finished (key computed first), so the worker never holds more than the pages
// that must be uploaded plus the one being filled. Anchors and page boundaries never depend on it. peakLiveBytes (optional)
// receives the largest live page capacity during the build (kept pages + the page just finished), for measurements.
struct Options {uint32_t anchorModulus=AnchorModulus,minFillVertices=MinFillVertices;const NorthlightMeshPageKey::KeySet* resident=nullptr;uint64_t* peakLiveBytes=nullptr;NorthlightWorldMesh::PreparationCache* preparation=nullptr;bool spooled=false,deviceLocal=false,pointCells=true;};
struct Page {
    std::vector<NorthlightGI::WorldVertex> vertices;
    // Page-local indices fit losslessly in 16 bits; source indices stay 32-bit.
    std::vector<uint16_t> indices;
    NorthlightMeshPageKey::Key key; // 0.3.216: content key of vertices+indices, set by build() on the worker
    // 0.3.216: the GPU already holds exactly this page (its key was resident when the plan was built): vertices/indices
    // were freed on the worker, key and exact byte sizes remain. The render thread must find it in the buffer store.
    bool elided=false,spooled=false;uint64_t fileOffset=0;
    size_t vertexCount()const{return key.vertexBytes/sizeof(NorthlightGI::WorldVertex);}
    size_t indexCount()const{return key.indexBytes/sizeof(uint16_t);}
};
struct Batch : NorthlightWorldMesh::Batch {
    uint32_t page=0,minVertex=0,vertexCount=0;
};
// Both ranges use page-local indices and the same triangle-list binding.
inline bool appendDrawRange(Batch& run,const Batch& next,unsigned primitiveLimit){
    if(run.page!=next.page||uint64_t(run.start)+3ull*run.count!=next.start)return false;
    const auto low=std::min(run.minVertex,next.minVertex);
    const uint64_t high=std::max(uint64_t(run.minVertex)+run.vertexCount,uint64_t(next.minVertex)+next.vertexCount);
    const uint64_t count=uint64_t(run.count)+next.count;
    if(high>UINT32_MAX||count>UINT32_MAX||(primitiveLimit&&count>primitiveLimit))return false;
    run.count=uint32_t(count);
    run.minVertex=low;
    run.vertexCount=uint32_t(high-low);
    return true;
}
struct PointRange {uint32_t first=0,count=0;};
// Immutable broad phase, built with the upload pages on the worker. Queries
// only reject whole distant subtrees; callers keep their original face tests
// and batch order. Unknown bounds always reach those tests.
class PointTree {
    struct Node {NorthlightGI::Vec3 low,high;uint32_t end=0,first=0,count=0;};
    static constexpr unsigned LeafSize=8;
    static size_t nodeLimit(size_t count){
        if(!count)return 0;
        size_t leaves=1;
        while(leaves*LeafSize<count)leaves*=2;
        return 2*leaves-1;
    }
    std::vector<Node> nodes;
    std::vector<uint32_t> order;
    uint32_t validCount=0;
    template<class Batches> void split(uint32_t first,uint32_t count,const Batches& batches){
        const uint32_t index=uint32_t(nodes.size());nodes.emplace_back();
        auto low=batches[order[first]].boundsLow,high=batches[order[first]].boundsHigh;
        for(uint32_t n=first+1;n<first+count;++n){const auto& b=batches[order[n]];
            low.x=std::min(low.x,b.boundsLow.x);low.y=std::min(low.y,b.boundsLow.y);low.z=std::min(low.z,b.boundsLow.z);
            high.x=std::max(high.x,b.boundsHigh.x);high.y=std::max(high.y,b.boundsHigh.y);high.z=std::max(high.z,b.boundsHigh.z);}
        nodes[index].low=low;nodes[index].high=high;
        if(count<=LeafSize){nodes[index].first=first;nodes[index].count=count;}
        else{
            const double extent[]={double(high.x)-low.x,double(high.y)-low.y,double(high.z)-low.z};
            const unsigned axis=extent[1]>extent[0]?(extent[2]>extent[1]?2:1):(extent[2]>extent[0]?2:0);
            const auto center=[&](uint32_t id){const auto& b=batches[id];
                return axis==0?double(b.boundsLow.x)+b.boundsHigh.x:axis==1?double(b.boundsLow.y)+b.boundsHigh.y:double(b.boundsLow.z)+b.boundsHigh.z;};
            const uint32_t half=count/2;
            std::nth_element(order.begin()+first,order.begin()+first+half,order.begin()+first+count,
                [&](uint32_t a,uint32_t b){const double x=center(a),y=center(b);return x<y||(x==y&&a<b);});
            split(first,half,batches);split(first+half,count-half,batches);
        }
        nodes[index].end=uint32_t(nodes.size());
    }
public:
    size_t bytes()const{return sizeof(*this)+nodes.capacity()*sizeof(Node)+order.capacity()*sizeof(uint32_t);}
    static std::shared_ptr<const PointTree> build(const std::vector<Batch>& batches,const std::vector<Batch>& subBatches,const NorthlightGI::AllocationAdmission& admit={}){
        struct View {
            const std::vector<Batch>& coarse;const std::vector<Batch>& fine;
            size_t size()const{return coarse.size()+fine.size();}
            const Batch& operator[](size_t i)const{return i<coarse.size()?coarse[i]:fine[i-coarse.size()];}
        };
        return build(View{batches,subBatches},admit);
    }
    template<class Batches> static std::shared_ptr<const PointTree> build(const Batches& batches,const NorthlightGI::AllocationAdmission& admit={}){
        if(batches.size()<16||batches.size()>65536)return {};
        if(admit&&!admit(sizeof(PointTree)+nodeLimit(batches.size())*sizeof(Node)+batches.size()*sizeof(uint32_t)+64))return {};
        try{
            auto tree=std::make_shared<PointTree>();tree->order.resize(batches.size());
            const auto valid=[](const auto& b){return std::isfinite(b.boundsLow.x)&&std::isfinite(b.boundsLow.y)&&std::isfinite(b.boundsLow.z)&&
                std::isfinite(b.boundsHigh.x)&&std::isfinite(b.boundsHigh.y)&&std::isfinite(b.boundsHigh.z)&&
                b.boundsLow.x<=b.boundsHigh.x&&b.boundsLow.y<=b.boundsHigh.y&&b.boundsLow.z<=b.boundsHigh.z;};
            size_t unknown=batches.size();
            for(uint32_t i=0;i<batches.size();++i){
                if(valid(batches[i]))tree->order[tree->validCount++]=i;
                else tree->order[--unknown]=i;
            }
            tree->nodes.reserve(nodeLimit(tree->validCount));
            if(tree->validCount)tree->split(0,tree->validCount,batches);
            return tree;
        }catch(const std::bad_alloc&){return {};}
    }
    template<class Reject> bool nearby(size_t batches,Reject reject,std::vector<unsigned char>& hits)const{
        if(order.size()!=batches)return false;
        try{hits.assign(batches,0);}catch(const std::bad_alloc&){return false;}
        for(size_t at=0;at<nodes.size();){const auto& node=nodes[at];
            if(reject(node.low,node.high)){at=node.end;continue;}
            for(uint32_t i=node.first;i<node.first+node.count;++i)hits[order[i]]=1;
            ++at;
        }
        for(size_t at=validCount;at<order.size();++at)hits[order[at]]=1;
        return true;
    }
};
// Worker-only partition inside one material/page. Triangle corners, attributes,
// coarse bounds and page boundaries stay identical. Lamp enclosures stay whole.
inline bool partitionPointBatch(Page& page,const Batch& original,std::vector<Batch>& output,PointRange& range,
                                const NorthlightGI::AllocationAdmission& admit={}){
    try {
    constexpr size_t Limit=65536;
    if(original.terrain||original.lampSourceId||original.count<=96||output.size()>=Limit)return true;
    const auto extent=original.boundsHigh-original.boundsLow;
    if(std::max({extent.x,extent.y,extent.z})<=16)return true;
    struct Triangle {uint64_t cell=0;uint32_t order=0;uint16_t ids[3]{};};
    if(admit&&!admit(uint64_t(original.count)*sizeof(Triangle)))return true;
    std::vector<Triangle> triangles;triangles.reserve(original.count);
    for(uint32_t at=0;at<original.count;++at){
        Triangle t;t.order=at;double center[3]{};
        for(unsigned k=0;k<3;++k){t.ids[k]=page.indices[original.start+at*3+k];const auto& p=page.vertices[t.ids[k]].position;
            center[0]+=double(p.x)/3;center[1]+=double(p.y)/3;center[2]+=double(p.z)/3;}
        for(unsigned axis=0;axis<3;++axis){const double cell=std::floor(center[axis]/16);
            if(cell< -1048576||cell>=1048576)return true; // Unrepresentable coordinates keep the original range.
            t.cell|=uint64_t(int64_t(cell)+1048576)<<(axis*21);}
        triangles.push_back(t);
    }
    std::sort(triangles.begin(),triangles.end(),[](const Triangle& a,const Triangle& b){return a.cell<b.cell||(a.cell==b.cell&&a.order<b.order);});
    size_t groups=1;for(size_t i=1;i<triangles.size();++i)groups+=triangles[i].cell!=triangles[i-1].cell;
    if(groups==1||groups>Limit-output.size())return true;
    if(output.size()+groups>output.capacity()){
        const size_t capacity=std::max(output.size()+groups,std::min(Limit,std::max<size_t>(1,output.capacity()*2)));
        if(admit&&!admit(capacity*sizeof(Batch)))return true;
        output.reserve(capacity);
    }
    range.first=uint32_t(output.size());range.count=uint32_t(groups);
    uint32_t at=original.start;
    for(size_t first=0;first<triangles.size();){
        size_t end=first+1;while(end<triangles.size()&&triangles[end].cell==triangles[first].cell)++end;
        Batch b=original;b.start=at;b.count=uint32_t(end-first);b.minVertex=UINT32_MAX;uint32_t high=0;
        const float inf=std::numeric_limits<float>::infinity();b.boundsLow={inf,inf,inf};b.boundsHigh={-inf,-inf,-inf};
        for(size_t i=first;i<end;++i)for(auto id:triangles[i].ids){
            page.indices[at++]=id;b.minVertex=std::min(b.minVertex,uint32_t(id));high=std::max(high,uint32_t(id));
            const auto& p=page.vertices[id].position;
            b.boundsLow.x=std::min(b.boundsLow.x,p.x);b.boundsLow.y=std::min(b.boundsLow.y,p.y);b.boundsLow.z=std::min(b.boundsLow.z,p.z);
            b.boundsHigh.x=std::max(b.boundsHigh.x,p.x);b.boundsHigh.y=std::max(b.boundsHigh.y,p.y);b.boundsHigh.z=std::max(b.boundsHigh.z,p.z);
        }
        b.vertexCount=high-b.minVertex+1;output.push_back(b);first=end;
    }
    return true;
    }catch(const std::bad_alloc&){return true;} // Optional metadata: preserve the coarse draw.
}
struct Plan {
    bool deviceLocal=false;
    std::shared_ptr<NorthlightGI::TemporaryGeometryFile> temporary;
    bool defaultPool()const{return deviceLocal||bool(temporary);}
    std::vector<Page> pages;
    std::vector<Batch> batches,pointSubBatches;
    std::vector<PointRange> pointSubRanges;
    std::shared_ptr<const PointTree> pointTree;
    uint64_t vertexBytes=0,indexBytes=0;
    bool copy(const Page& page,bool vertices,size_t offset,size_t bytes,void* target)const{
        const uint64_t size=vertices?page.key.vertexBytes:page.key.indexBytes;
        if(page.elided||!target||offset>size||bytes>size-offset)return false;
        if(!page.spooled){if(size!=(vertices?page.vertices.size()*sizeof(NorthlightGI::WorldVertex):page.indices.size()*sizeof(uint16_t)))return false;std::memcpy(target,(vertices?reinterpret_cast<const char*>(page.vertices.data()):reinterpret_cast<const char*>(page.indices.data()))+offset,bytes);return true;}
        if(!temporary||page.fileOffset>temporary->size()||uint64_t(page.key.vertexBytes)+page.key.indexBytes>temporary->size()-page.fileOffset)return false;
        try{temporary->read(page.fileOffset+(vertices?0:page.key.vertexBytes)+offset,target,bytes);return true;}catch(const std::exception&){return false;}
    }
    bool copyMaterial(const NorthlightWorldMesh::MaterialUpload& material,size_t offset,size_t bytes,void* target)const{
        const auto size=material.pixelBytes();if(!target||offset>size||bytes>size-offset)return false;
        if(!material.spooled){std::memcpy(target,material.pixels().data()+offset,bytes);return true;}
        if(!temporary||material.fileOffset>temporary->size()||size>temporary->size()-material.fileOffset)return false;
        try{temporary->read(material.fileOffset+offset,target,bytes);return true;}catch(const std::exception&){return false;}
    }
    void accountMemory(NorthlightMemoryFootprint::Counter& memory,bool includeObject=true)const{
        using namespace NorthlightMemoryFootprint;
        if(includeObject){if(!memory.visit(this))return;memory.add(Metadata,sizeof(*this));}
        memory.vector(Metadata,pages);memory.vector(Metadata,batches);memory.vector(Metadata,pointSubBatches);memory.vector(Metadata,pointSubRanges);
        if(pointTree&&memory.visit(pointTree.get()))memory.add(Metadata,pointTree->bytes());
        for(const auto& page:pages){memory.vector(Upload,page.vertices);memory.vector(Upload,page.indices);}
    }
    uint64_t cpuBytes() const {
        uint64_t bytes=sizeof(*this)+pages.capacity()*sizeof(Page)+batches.capacity()*sizeof(Batch)+pointSubBatches.capacity()*sizeof(Batch)+pointSubRanges.capacity()*sizeof(PointRange);
        if(pointTree)bytes+=pointTree->bytes();
        for(const auto& p:pages)bytes+=p.vertices.capacity()*sizeof(NorthlightGI::WorldVertex)+p.indices.capacity()*sizeof(uint16_t);
        return bytes;
    }
};

// At most one page (or its preflight's first overflowing vertex) is indexed.
// Contiguous slots avoid allocating/freeing a hash node for every vertex.
class IndexMap {
    static constexpr uint32_t Capacity=2*PageVertexLimit;
    struct Slot {uint32_t key=0,value=0,epoch=0;};
    std::vector<Slot> slots_=std::vector<Slot>(Capacity);
    uint32_t epoch_=1,size_=0;mutable uint32_t recentKey_=0,recentSlot_=0,recentEpoch_=0;
    uint32_t slot(uint32_t key)const {
        if(recentEpoch_==epoch_&&recentKey_==key)return recentSlot_;
        uint32_t i=(key*0x9e3779b9u)>>(32-15);
        while(slots_[i].epoch==epoch_&&slots_[i].key!=key)i=(i+1)&(Capacity-1);
        recentKey_=key;recentSlot_=i;recentEpoch_=epoch_;return i;
    }
public:
    static_assert(Capacity==32768,"Index hash uses the upper 15 bits");
    static constexpr uint32_t Missing=UINT32_MAX;
    void clear(){recentEpoch_=0;if(!++epoch_){std::fill(slots_.begin(),slots_.end(),Slot{});epoch_=1;}size_=0;}
    uint32_t size()const{return size_;}
    uint32_t find(uint32_t key)const {const auto& s=slots_[slot(key)];return s.epoch==epoch_?s.value:Missing;}
    std::pair<uint32_t,bool> insert(uint32_t key,uint32_t value) {
        auto& s=slots_[slot(key)];if(s.epoch==epoch_)return {s.value,false};
        s={key,value,epoch_};++size_;return {value,true};
    }
};

// The input indices/batches must be the immutable packed upload plan. Every
// original batch stays whole if it fits an empty page. Larger batches split
// only at triangle boundaries. One bounded source-index map is reused per page;
// a second bounded set preflights a batch without retaining a scene-sized map.
// Failure never modifies output. Materials and scene ownership remain in input.
// Pages also close at content anchors (see Options) and carry a content key; a batch that fits an empty page
// is never split by an anchor.
template<bool Segmented> inline bool buildImpl(const NorthlightWorldMesh::WorldMeshUploadPlan& input,Plan& output,std::string& error,
                  const NorthlightGI::AllocationAdmission& admit = {},const Options& options = {}) {
    auto fail=[&](const char* why){error=why;return false;};
    try {
        if((input.pagingIndices&&!input.indices.empty())||bool(input.pagingIndices)!=bool(input.pagingChunks)||input.pagingIndexCount()!=uint64_t(input.triangleCount)*3)
            return fail("Paged mesh triangle/index count mismatch");
        if(input.pagesSealed||((input.pagingVertices||input.pagingChunks)&&(input.source||input.ownedSource))||(input.pagingVertices&&input.pagingChunks))
            return fail("Paged mesh source ownership mismatch");
        if(input.pagingIndexCount()&&(!input.hasPagingVertices()||input.pagingVertexCount()!=input.vertexCount))
            return fail("Paged mesh source/vertex count mismatch");
        Plan next;next.deviceLocal=options.deviceLocal;if(options.spooled)next.temporary=input.temporary;
        if(!input.pagingIndexCount()){
            if(!input.batches.empty())return fail("Paged mesh empty indices with batches");
            output=std::move(next);error.clear();return true;
        }
        const auto* vertices=input.verticesForPaging();const auto* vertexChunks=input.pagingChunks.get();
        const auto* indexData=input.indices.data();const auto* indexChunks=input.pagingIndices.get();
        const auto vertexAt=[&](size_t i)->const NorthlightGI::WorldVertex&{if constexpr(Segmented)return (*vertexChunks)[i];else return (*vertices)[i];};
        const auto indexAt=[&](size_t i)->uint32_t{if constexpr(Segmented)return (*indexChunks)[i];else return indexData[i];};
        // Immutable layout memo has no CPU geometry. Require every page still
        // resident and the exact production partition options before reuse.
        if(options.pointCells&&options.preparation&&options.resident&&options.anchorModulus==AnchorModulus&&options.minFillVertices==MinFillVertices&&
           options.preparation->reuseResident(input,output,*options.resident,admit)){output.deviceLocal=options.deviceLocal;if(options.peakLiveBytes)*options.peakLiveBytes=0;error.clear();return true;}
        auto allowed=[&](uint64_t bytes){return !admit||admit(bytes);};
        // Reserve admission for both bounded index tables.
        if(!allowed(2u*1024u*1024u))return fail("Paged mesh working memory deferred");
        IndexMap remap,batchUnique;
        uint64_t expectedStart=0;
        bool closeAfter=false; // an anchor closed the current page: the next triangle/batch opens a new one
        uint64_t liveKept=0,peakLive=0;
        std::vector<NorthlightGI::WorldVertex> reusableVertices;std::vector<uint16_t> reusableIndices;
        // A finished page gets its content key; a resident one is freed right here (elided), keeping key and sizes.
        size_t pageBatchFirst=0;bool pointCells=options.pointCells;
        auto finishPage=[&](Page& p){
            if(pointCells){
                try{if(!allowed(next.batches.size()*sizeof(PointRange)))pointCells=false;
                    else next.pointSubRanges.resize(next.batches.size());
                }catch(const std::bad_alloc&){pointCells=false;}
                if(pointCells)for(size_t i=pageBatchFirst;i<next.batches.size();++i)
                    partitionPointBatch(p,next.batches[i],next.pointSubBatches,next.pointSubRanges[i],admit);
                else{std::vector<Batch>().swap(next.pointSubBatches);std::vector<PointRange>().swap(next.pointSubRanges);}
            }
            pageBatchFirst=next.batches.size();
            p.key=NorthlightMeshPageKey::keyOf(p.vertices.data(),p.vertices.size()*sizeof(NorthlightGI::WorldVertex),p.indices.data(),p.indices.size()*sizeof(uint16_t));
            next.vertexBytes+=p.key.vertexBytes;next.indexBytes+=p.key.indexBytes;
            uint64_t capacity=p.vertices.capacity()*sizeof(NorthlightGI::WorldVertex)+p.indices.capacity()*sizeof(uint16_t);
            peakLive=std::max(peakLive,liveKept+capacity);
            if(options.resident&&options.resident->count(p.key)){
                std::vector<NorthlightGI::WorldVertex>().swap(p.vertices);std::vector<uint16_t>().swap(p.indices);p.elided=true;
            }else if(options.spooled){
                if(!next.temporary)next.temporary=std::make_shared<NorthlightGI::TemporaryGeometryFile>();
                p.fileOffset=next.temporary->append(p.vertices.data(),p.key.vertexBytes);
                next.temporary->append(p.indices.data(),p.key.indexBytes);p.spooled=true;
                reusableVertices.swap(p.vertices);reusableIndices.swap(p.indices);reusableVertices.clear();reusableIndices.clear();
            }else{
                // Content anchors often close city pages half full. Keep the
                // exact bytes, not a full 512 KiB vertex capacity per page.
                // Only one bounded page is copied; resident pages skip it.
                // Optional: a refused or failed copy keeps the page as it is
                // (same bytes, larger capacity) instead of failing the build.
                auto compact=[&](auto& buffer){
                    using Buffer=std::decay_t<decltype(buffer)>;
                    using Value=typename Buffer::value_type;
                    const uint64_t oldBytes=buffer.capacity()*sizeof(Value),bytes=buffer.size()*sizeof(Value);
                    if(buffer.capacity()<=buffer.size()+buffer.size()/4||oldBytes-bytes<65536)return;
                    if(!allowed(bytes))return;
                    try{
                        Buffer exact(buffer.begin(),buffer.end());
                        const uint64_t newBytes=exact.capacity()*sizeof(Value);
                        peakLive=std::max(peakLive,liveKept+capacity+newBytes);
                        buffer.swap(exact);capacity=capacity-oldBytes+newBytes;
                    }catch(const std::bad_alloc&){}
                };
                compact(p.vertices);compact(p.indices);
                liveKept+=capacity;
            }
            return true;
        };
        auto newPage=[&](){
            closeAfter=false;
            if(!next.pages.empty()&&!finishPage(next.pages.back()))return false;
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
            if(options.spooled){next.pages.back().vertices.swap(reusableVertices);next.pages.back().indices.swap(reusableIndices);next.pages.back().indices.reserve(PageIndexLimit);}
            next.pages.back().vertices.reserve(PageVertexLimit);
            remap.clear();return true;
        };
        for(const auto& original:input.batches) {
            const uint64_t count=uint64_t(original.count)*3;
            if(!original.count||original.start!=expectedStart||count>input.pagingIndexCount()-expectedStart||original.material>=input.materials.size())
                return fail("Paged mesh malformed batch range/material");
            expectedStart+=count;
            // Preflight only batches that could fit; the set never exceeds one
            // page plus one entry even for an arbitrarily large original batch.
            bool fitsEmpty=count<=PageIndexLimit;
            uint32_t missing=0;
            batchUnique.clear();
            if(fitsEmpty) {
                for(uint64_t i=original.start;i<expectedStart;++i) {
                    const auto sourceIndex=indexAt(size_t(i));
                    if(sourceIndex>=input.vertexCount)return fail("Paged mesh index outside source vertices");
                    if(batchUnique.insert(sourceIndex,0).second) {
                        if(batchUnique.size()>PageVertexLimit){fitsEmpty=false;break;}
                        if(!next.pages.empty()&&!closeAfter&&remap.find(sourceIndex)==IndexMap::Missing)++missing;
                    }
                }
            }
            bool anchored=false; // anchor hit inside a batch that must stay whole: the page closes after the batch
            if(next.pages.empty()||closeAfter||(fitsEmpty&&(next.pages.back().indices.size()+count>PageIndexLimit||next.pages.back().vertices.size()+missing>PageVertexLimit)))
                if(!newPage())return fail("Paged mesh page allocation deferred");
            size_t currentBatch=std::numeric_limits<size_t>::max();
            for(uint64_t i=original.start;i<expectedStart;i+=3) {
                uint32_t ids[3],locals[3],newVertices=0;
                for(unsigned k=0;k<3;++k) {
                    ids[k]=indexAt(size_t(i+k));
                    if(ids[k]>=input.vertexCount)return fail("Paged mesh index outside source vertices");
                    bool repeated=false;for(unsigned previous=0;previous<k;++previous)repeated|=ids[k]==ids[previous];
                    locals[k]=remap.find(ids[k]);
                    if(!repeated&&locals[k]==IndexMap::Missing)++newVertices;
                }
                if(closeAfter||next.pages.back().vertices.size()+newVertices>PageVertexLimit||next.pages.back().indices.size()+3>PageIndexLimit) {
                    if(!newPage())return fail("Paged mesh page allocation deferred");
                    for(auto& local:locals)local=IndexMap::Missing;
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
                    uint32_t local=locals[k];
                    if(local==IndexMap::Missing) {
                        auto insertion=remap.insert(ids[k],uint32_t(page.vertices.size()));local=insertion.first;
                        if(insertion.second) {
                            const auto& vertex=vertexAt(ids[k]);
                            if(!std::isfinite(vertex.position.x)||!std::isfinite(vertex.position.y)||!std::isfinite(vertex.position.z))
                                return fail("Paged mesh nonfinite vertex position");
                            page.vertices.push_back(vertex);
                        }
                    }
                    page.indices.push_back(uint16_t(local));
                    batch.minVertex=std::min(batch.minVertex,local);maxVertex=std::max(maxVertex,local);
                    const auto& p=page.vertices[local].position;
                    batch.boundsLow.x=std::min(batch.boundsLow.x,p.x);batch.boundsLow.y=std::min(batch.boundsLow.y,p.y);batch.boundsLow.z=std::min(batch.boundsLow.z,p.z);
                    batch.boundsHigh.x=std::max(batch.boundsHigh.x,p.x);batch.boundsHigh.y=std::max(batch.boundsHigh.y,p.y);batch.boundsHigh.z=std::max(batch.boundsHigh.z,p.z);
                }
                batch.vertexCount=maxVertex-batch.minVertex+1;++batch.count;
                if(options.anchorModulus&&page.vertices.size()>=options.minFillVertices) {
                    const size_t end=page.indices.size();
                    const uint64_t anchor=NorthlightMeshPageKey::triangleHash(&page.vertices[page.indices[end-3]].position.x,&page.vertices[page.indices[end-2]].position.x,&page.vertices[page.indices[end-1]].position.x);
                    if(anchor%options.anchorModulus==0){if(fitsEmpty)anchored=true;else closeAfter=true;}
                }
            }
            if(anchored)closeAfter=true;
        }
        if(expectedStart!=input.pagingIndexCount())return fail("Paged mesh batches do not cover all indices");
        if(!next.pages.empty()&&!finishPage(next.pages.back()))return fail("Point shadow range allocation deferred");
        next.pointTree=PointTree::build(next.batches,next.pointSubBatches,admit);
        if(next.temporary)next.temporary->flush();
        if(options.peakLiveBytes)*options.peakLiveBytes=peakLive;
        if(options.pointCells&&options.preparation&&options.anchorModulus==AnchorModulus&&options.minFillVertices==MinFillVertices)options.preparation->rememberPages(input,next);
        output=std::move(next);error.clear();return true;
    } catch(const std::bad_alloc&) {return fail("Paged mesh worker allocation failed");}
      catch(const std::length_error&) {return fail("Paged mesh worker allocation size overflow");}
      catch(const std::runtime_error&) {return fail("Paged mesh temporary storage failed");}
}
inline bool build(const NorthlightWorldMesh::WorldMeshUploadPlan& input,Plan& output,std::string& error,
                  const NorthlightGI::AllocationAdmission& admit = {},const Options& options = {}) {
    return input.pagingChunks?buildImpl<true>(input,output,error,admit,options):buildImpl<false>(input,output,error,admit,options);
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
        stats.bytes+=page.vertices.capacity()*sizeof(NorthlightGI::WorldVertex)+page.indices.capacity()*sizeof(uint16_t);
        std::vector<NorthlightGI::WorldVertex>().swap(page.vertices);std::vector<uint16_t>().swap(page.indices);
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
        if(page.spooled&&(!pages.temporary||page.fileOffset>pages.temporary->size()||uint64_t(page.key.vertexBytes)+page.key.indexBytes>pages.temporary->size()-page.fileOffset))return "mesh page temporary range invalid";
        if((page.elided||page.spooled)?!page.vertices.empty()||!page.indices.empty():page.key.vertexBytes!=page.vertices.size()*sizeof(NorthlightGI::WorldVertex)||page.key.indexBytes!=page.indices.size()*sizeof(uint16_t))return "mesh page key size mismatch";
        if(!page.vertexCount()||!page.indexCount()||page.key.vertexBytes%sizeof(NorthlightGI::WorldVertex)||page.key.indexBytes%sizeof(uint16_t)||page.vertexCount()>PageVertexLimit||page.indexCount()>PageIndexLimit||page.indexCount()%3)return "invalid mesh page size";
        vertexBytes+=page.key.vertexBytes;indexBytes+=page.key.indexBytes;
    }
    if(vertexBytes!=pages.vertexBytes||indexBytes!=pages.indexBytes||indexBytes*2!=input.indexBytes)return "paged aggregate sizes mismatch";
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
    if(!pages.pointSubRanges.empty()&&pages.pointSubRanges.size()!=pages.batches.size())return "point range parent count mismatch";
    size_t nextPoint=0;
    for(size_t i=0;i<pages.pointSubRanges.size();++i){const auto& range=pages.pointSubRanges[i];const auto& parent=pages.batches[i];
        if(!range.count)continue;
        if(parent.terrain||parent.lampSourceId||range.count<2||range.first!=nextPoint||range.count>pages.pointSubBatches.size()-nextPoint)return "invalid point range parent";
        uint64_t start=parent.start;
        for(size_t n=0;n<range.count;++n){const auto& b=pages.pointSubBatches[nextPoint++];
            if(b.page!=parent.page||b.start!=start||!b.count||b.material!=parent.material||b.terrain||b.lampSourceId||b.pointOnly!=parent.pointOnly||
               b.chunkX!=parent.chunkX||b.chunkY!=parent.chunkY||!b.vertexCount||b.minVertex<parent.minVertex||uint64_t(b.minVertex)+b.vertexCount>uint64_t(parent.minVertex)+parent.vertexCount)return "invalid point sub-batch";
            for(unsigned axis=0;axis<3;++axis)if(!std::isfinite((&b.boundsLow.x)[axis])||!std::isfinite((&b.boundsHigh.x)[axis])||(&b.boundsLow.x)[axis]>(&b.boundsHigh.x)[axis]||
                (&b.boundsLow.x)[axis]<(&parent.boundsLow.x)[axis]||(&b.boundsHigh.x)[axis]>(&parent.boundsHigh.x)[axis])return "invalid point sub-batch bounds";
            start+=uint64_t(b.count)*3;
        }
        if(start!=uint64_t(parent.start)+uint64_t(parent.count)*3)return "incomplete point sub-batch coverage";
    }
    if(nextPoint!=pages.pointSubBatches.size()||nextPoint>65536)return "incomplete point range coverage";
    for(size_t i=0;i<materials.size();++i){const auto& material=materials[i];
        if(!material.width||!material.height||material.width>4096||material.height>4096||uint64_t(material.width)*material.height*material.stride()!=material.pixelBytes())return "invalid paged texture size";
        if(material.spooled&&(!material.pixels().empty()||!pages.temporary||material.fileOffset>pages.temporary->size()||material.pixelBytes()>pages.temporary->size()-material.fileOffset))return "invalid paged texture temporary range";
        if(material.textureOwner!=UINT32_MAX){
            if(material.textureOwner>=i)return "invalid paged texture owner";
            const auto& owner=materials[material.textureOwner];
            if(owner.textureOwner!=UINT32_MAX||owner.width!=material.width||owner.height!=material.height||
               !(owner.textureIdentity==material.textureIdentity)||owner.spooled!=material.spooled||owner.alphaOnly!=material.alphaOnly||
               (material.spooled?owner.fileOffset!=material.fileOffset:!owner.sharedBgra||owner.sharedBgra!=material.sharedBgra))return "invalid paged texture alias";
        }
    }
    return nullptr;
}
inline const char* validatePages(const NorthlightWorldMesh::WorldMeshUploadPlan& input,const Plan& pages) {return validatePages(input,pages,input.materials);}
// Render thread only (shares the payload). A released payload is not a sealed plan any more.
inline const char* validateSealed(const NorthlightWorldMesh::WorldMeshUploadPlan& input) {
    if(!input.pagesSealed||input.source||input.ownedSource||input.pagingVertices||input.pagingChunks||input.pagingIndices||!input.indices.empty())return "paged ownership not sealed";
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
    if(input.pagesSealed||!input.hasPagingVertices()||!pages)return fail("paged seal requires unsealed source and pages");
    if(((input.pagingVertices||input.pagingChunks)&&(input.source||input.ownedSource))||(input.pagingVertices&&input.pagingChunks))return fail("paged seal source ownership mismatch");
    if(input.ownedSource&&input.source!=input.ownedSource.get())return fail("paged seal source identity mismatch");
    if(input.pagingVertexCount()!=input.vertexCount||(input.pagingIndices&&!input.indices.empty())||bool(input.pagingIndices)!=bool(input.pagingChunks)||input.pagingIndexCount()!=uint64_t(input.triangleCount)*3)return fail("paged seal source counts mismatch");
    if(input.source&&(input.source->triangles.size()!=input.triangleCount||input.source->materials.size()!=input.materials.size()))return fail("paged seal source counts mismatch");
    if(const char* reason=validatePages(input,*pages))return fail(reason);
    try {
        const auto& sourceOwners=input.source?input.source->completePlacements:input.completePlacements;
        uint64_t ownerBytes=sourceOwners.size()*sizeof(NorthlightGI::WorldPlacementCoverage);
        for(const auto& owner:sourceOwners)ownerBytes+=owner.modelKey.size()+1;
        if(admit&&!admit(ownerBytes))return fail("paged ownership allocation deferred");
        auto owners=sourceOwners;
        const uint64_t cpuBytes=pages->cpuBytes();
        std::vector<uint64_t> pixelOffsets;
        if(pages->temporary){
            pixelOffsets.resize(input.materials.size());
            for(size_t i=0;i<input.materials.size();++i){const auto& material=input.materials[i];
                if(material.pixels().size()>4)pixelOffsets[i]=material.textureOwner!=UINT32_MAX?pixelOffsets[material.textureOwner]:pages->temporary->append(material.pixels().data(),material.pixels().size());
            }
            pages->temporary->flush();
        }
        // 0.3.214 (task 42): pages and cutout textures move into the separately owned payload.
        auto payload=std::make_shared<NorthlightWorldMesh::UploadPayload>();
        payload->pageMetadataBytes=sizeof(*pages)+pages->pages.capacity()*sizeof(Page)+pages->batches.capacity()*sizeof(Batch)+pages->pointSubBatches.capacity()*sizeof(Batch)+pages->pointSubRanges.capacity()*sizeof(PointRange);
        if(pages->pointTree)payload->pageMetadataBytes+=pages->pointTree->bytes();
        for(const auto& page:pages->pages)payload->pageStorageBytes+=page.vertices.capacity()*sizeof(NorthlightGI::WorldVertex)+page.indices.capacity()*sizeof(uint16_t);
        payload->materialMetadataBytes=input.materials.capacity()*sizeof(NorthlightWorldMesh::MaterialUpload);
        for(const auto& material:input.materials)if(!pages->temporary||material.pixels().size()<=4)payload->materialPixelBytes+=material.retainedPixelBytes();
        payload->bytes=cpuBytes+sizeof(NorthlightWorldMesh::UploadPayload)+input.materials.capacity()*sizeof(NorthlightWorldMesh::MaterialUpload)+64;
        payload->bytes+=payload->materialPixelBytes;
        if(pages->temporary)for(size_t i=0;i<input.materials.size();++i){auto& material=input.materials[i];if(material.pixels().size()>4){material.fileOffset=pixelOffsets[i];material.spooled=true;std::vector<uint8_t>().swap(material.bgra);material.sharedBgra.reset();}}
        payload->pages=std::move(pages);payload->materials.swap(input.materials);
        input.completePlacements=std::move(owners);input.payload.set(std::move(payload));input.pagedCpuBytes=cpuBytes;
        input.source=nullptr;input.ownedSource.reset();input.pagingVertices.reset();input.pagingChunks.reset();input.pagingIndices.reset();input.temporary.reset();input.preparationLayout.reset();std::vector<uint32_t>().swap(input.indices);
        input.pagesSealed=true;error.clear();return true;
    } catch(const NorthlightGI::TemporaryGeometryError&) {return fail("paged ownership temporary storage failed");}
      catch(const std::bad_alloc&) {return fail("paged ownership allocation failed");}
      catch(const std::length_error&) {return fail("paged ownership size overflow");}
}
} // namespace NorthlightWorldMeshPages
