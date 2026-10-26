#include "draw_snapshot.h"
#include <atomic>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <new>

static bool refuseAllocation=false;
#if !__has_feature(thread_sanitizer)
void* operator new(size_t n){if(refuseAllocation)throw std::bad_alloc();if(auto* p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void operator delete(void* p)noexcept{std::free(p);}
void operator delete(void* p,size_t)noexcept{std::free(p);}
#endif
namespace NorthlightDrawSnapshot {
struct SnapshotTestAccess {
    static std::shared_ptr<const Mesh> seed(Frame& frame,IDirect3DVertexDeclaration9* declaration){
        std::shared_ptr<const Mesh> held;
        for(unsigned i=0;i<20;++i){
            Mesh mesh;mesh.streams[0].stride=16;mesh.streams[0].bytes.assign(4u<<20,static_cast<unsigned char>(i+1));
            mesh.vertexCount=3;mesh.primitiveCount=1;
            auto entry=std::make_unique<Frame::CacheEntry>();entry->key.vb[0]=i+1;entry->producer=&frame;
            entry->declaration.p=declaration;declaration->AddRef();entry->rawIndices.assign(512u<<10,1);
            entry->metadataBytes=sizeof(Frame::CacheEntry)+entry->rawIndices.capacity()+96;
            entry->bytes=entry->metadataBytes+NorthlightSourceIdentity::allocationBytes<Mesh>(mesh.capacityBytes());
            entry->mesh=frame.retain(std::move(mesh));entry->serial=frame.sources_->serial++;
            if(!held)held=entry->mesh;
            const auto bytes=entry->bytes,metadata=entry->metadataBytes;
            auto inserted=frame.sources_->entries.emplace(Frame::hashKey(entry->key),std::move(entry));assert(inserted.second);
            frame.sources_->lru.append(*inserted.first->second);frame.sources_->bytes+=bytes;frame.sources_->metadataBytes+=metadata;
            frame.predicted_=inserted.first->second.get();frame.predictionGeneration_=frame.sources_->generation;
        }
        assert(frame.sharedSourceBytes()<=frame.sharedSourceLimit());return held;
    }
    static bool invalidated(const Frame& frame){return !frame.predicted_||frame.predictionGeneration_!=frame.sources_->generation;}
};
}
struct Declaration:IDirect3DVertexDeclaration9 {
    unsigned refs=1,releases=0;std::thread::id owner=std::this_thread::get_id();
    unsigned AddRef()override{assert(std::this_thread::get_id()==owner);return ++refs;}
    unsigned Release()override{assert(std::this_thread::get_id()==owner&&"RELEASE_OWNER_THREAD");assert(refs);++releases;return --refs;}
    HRESULT GetDeclaration(D3DVERTEXELEMENT9*,UINT*)override{return D3DERR_INVALIDCALL;}
};
struct Blocker {
    std::atomic<bool>& entered;std::atomic<bool>& proceed;
    ~Blocker(){entered=true;while(!proceed)std::this_thread::yield();}
};
static void one(const char* name,bool defer,bool full,bool allocationFailure){
    const auto initial=NorthlightSourceIdentity::cpuLiveBytes.load();Declaration declaration;
    std::atomic<bool> entered{false},proceed{false};NorthlightStreaming::CpuRetirement queue(false);
    auto blocker=std::shared_ptr<Blocker>(new Blocker{entered,proceed});
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
    while(!queue.retire(blocker,full?128u<<20:1)){assert(std::chrono::steady_clock::now()<deadline);std::this_thread::yield();}
    while(!entered)std::this_thread::yield();
    {
        NorthlightDrawSnapshot::Frame frame,peer;assert(peer.shareSnapshotPool(frame));
        auto held=NorthlightDrawSnapshot::SnapshotTestAccess::seed(peer,&declaration);
        const auto before=frame.sharedSourceBytes();assert(before>90u<<20);
        refuseAllocation=allocationFailure;
        const auto start=std::chrono::steady_clock::now();
        const auto deferred=frame.clearIndexCache(defer?&queue:nullptr);
        const auto ns=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-start).count();
        refuseAllocation=false;
        assert(!frame.sharedSourceBytes()&&!peer.sharedSourceBytes()&&!frame.snapshotCacheEntries()&&!peer.indexCacheEntries());
        assert(frame.sharedSourceLimit()==128u<<20);
        assert(declaration.refs==1&&declaration.releases==20&&"COM_RELEASE_BEFORE_ENQUEUE");
        assert(NorthlightDrawSnapshot::SnapshotTestAccess::invalidated(peer));
        const bool queued=defer&&!full&&!allocationFailure;
        if(queued){assert(deferred==before&&"RETIREMENT_NOT_QUEUED");assert(frame.sourceLiveObjects()==20);}
        else {assert(!deferred);assert(frame.sourceLiveObjects()==1);}
        assert(held->streams[0].bytes.size()==4u<<20&&held->streams[0].bytes.front()==1&&held->streams[0].bytes.back()==1);
        proceed=true;assert(queue.waitIdle(10000));assert(frame.sourceLiveObjects()==1);
        held.reset();assert(!frame.sourceLiveObjects());
        // A fresh populated pool after retirement still clears synchronously.
        held=NorthlightDrawSnapshot::SnapshotTestAccess::seed(frame,&declaration);
        assert(!frame.clearIndexCache());assert(frame.sourceLiveObjects()==1);
        held.reset();assert(!frame.sourceLiveObjects()&&declaration.refs==1&&declaration.releases==40);
        std::printf("SNAPSHOT_RETIRE_ROW case=%s bytes=%zu deferred=%zu boundaryNs=%lld\n",name,before,deferred,static_cast<long long>(ns));
    }
    assert(NorthlightSourceIdentity::cpuLiveBytes.load()==initial);
}
int main(){
    one("demand",true,false,false);
    one("critical",false,false,false);
    one("queue-full",true,true,false);
#if !__has_feature(thread_sanitizer)
    one("allocation-failure",true,false,true); // TSan owns operator new; other modes inject failure.
#endif
    std::puts("SNAPSHOT_RETIRE_PASS owner_thread_COM bounded_queue critical_immediate shared_holders stale_prediction allocation_fallback ledger_drained rebuild");
}
