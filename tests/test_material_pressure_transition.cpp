#define NOMINMAX
#include <d3d9.h>
#include <cassert>
static unsigned pressureMemoryReads=0;
static BOOL WINAPI pressureMemoryStatus(MEMORYSTATUSEX* memory){
    ++pressureMemoryReads;assert(memory&&memory->dwLength==sizeof(*memory));memory->ullAvailVirtual=2048ull<<20;return TRUE;
}
#define GlobalMemoryStatusEx pressureMemoryStatus
#include "material_pressure_reference.h"
#undef GlobalMemoryStatusEx

namespace NorthlightDrawSnapshot {
struct SnapshotTestAccess {
    static std::shared_ptr<const Mesh> seed(Frame& frame,IDirect3DVertexDeclaration9* declaration,unsigned count=26,size_t payloadBytes=4u<<20){
        std::shared_ptr<const Mesh> held;
        for(unsigned i=0;i<count;++i){
            Mesh mesh;mesh.streams[0].stride=16;mesh.streams[0].bytes.assign(payloadBytes,static_cast<unsigned char>(i+1));mesh.vertexCount=3;mesh.primitiveCount=1;
            auto entry=std::make_unique<Frame::CacheEntry>();entry->key.vb[0]=i+1;entry->key.declaration=declaration;entry->producer=&frame;
            entry->declaration.p=declaration;declaration->AddRef();
            entry->metadataBytes=sizeof(Frame::CacheEntry)+96;
            entry->bytes=entry->metadataBytes+NorthlightSourceIdentity::allocationBytes<Mesh>(mesh.capacityBytes());
            entry->mesh=frame.retain(std::move(mesh));entry->serial=frame.sources_->serial++;
            if(!held)held=entry->mesh;
            const auto bytes=entry->bytes,metadata=entry->metadataBytes;
            const auto hash=Frame::hashKey(entry->key);auto added=frame.sources_->entries.emplace(hash,std::move(entry));assert(added.second);
            frame.sources_->lru.append(*added.first->second);frame.sources_->bytes+=bytes;frame.sources_->metadataBytes+=metadata;
            frame.predicted_=added.first->second.get();frame.predictionGeneration_=frame.sources_->generation;
        }
        assert(frame.sharedSourceBytes()<=frame.sharedSourceLimit());return held;
    }
    static bool predictionCleared(const Frame& frame){return frame.predicted_==nullptr;}
};
}
struct PressureDeclaration:Declaration {
    DWORD thread=GetCurrentThreadId();unsigned releases=0;
    ULONG STDMETHODCALLTYPE Release()override{assert(GetCurrentThreadId()==thread);++releases;return Declaration::Release();}
};
void MaterialCaptureStateTestAccess::pressureTransition(){
    CaptureBackend device;PressureDeclaration declaration;
    const auto initialCpu=NorthlightSourceIdentity::cpuLiveBytes.load();
    unsigned pressureReads=0,trimReads=0,recoveryReads=0,evictions=0,geometryEvictions=0;size_t before=0,after=0;
    {
        NorthlightAutoBump bump(&device);NorthlightDrawSnapshot::Frame worldPeer(2u<<20,0);
        assert(worldPeer.shareSnapshotPool(bump.deferredSnapshots));
        auto held=NorthlightDrawSnapshot::SnapshotTestAccess::seed(bump.deferredSnapshots,&declaration);
        assert(bump.deferredSnapshots.sharedSourceParticipants()==3&&bump.deferredSnapshots.sharedSourceLimit()==(112u<<20));
        // Match the real guard: world caps shrink first, then material caps.
        worldPeer.setSnapshotCacheLimit(32u<<20);bump.deferredSnapshots.clearFrame();before=bump.deferredSnapshots.sharedSourceBytes();
        assert(before>(60u<<20)&&before<=(80u<<20));
        const auto queries=pressureMemoryReads;failAllocationAfter=0;bump.setMemoryPressure(true);assert(failAllocationAfter==0);failAllocationAfter=-1;
        pressureReads=pressureMemoryReads-queries;evictions=bump.deferredSnapshots.maintenance().snapshotEvictions;after=bump.deferredSnapshots.sharedSourceBytes();
        assert(bump.memoryPressure&&!after&&!bump.deferredSnapshots.snapshotCacheEntries());
        assert(bump.deferredSnapshots.snapshotCacheLimit()==(4u<<20)&&DisplacementStateTestAccess::pressureLimit(bump.displacement)==(6u<<20));
        assert(bump.deferredSnapshots.sharedSourceLimit()==(42u<<20)&&!bump.deferredGPU.limit()&&!bump.deferredGPU.chargedBytes());
        assert(NorthlightDrawSnapshot::SnapshotTestAccess::predictionCleared(bump.deferredSnapshots));
        assert(declaration.refs==1&&declaration.releases==26);
        assert(held->streams[0].bytes.size()==(4u<<20)&&std::all_of(held->streams[0].bytes.begin(),held->streams[0].bytes.end(),[](unsigned char x){return x==1;}));
        assert(bump.deferredSnapshots.sourceLiveObjects()==1); // escaped immutable ownership stays valid
        const auto repeatQueries=pressureMemoryReads;bump.setMemoryPressure(true);assert(pressureMemoryReads==repeatQueries);
        const auto trimQueries=pressureMemoryReads;failAllocationAfter=0;bump.trimMemory();assert(failAllocationAfter==0);failAllocationAfter=-1;trimReads=pressureMemoryReads-trimQueries;
        assert(!bump.deferredSnapshots.sharedSourceBytes()&&bump.deferredSnapshots.sharedSourceLimit()==(42u<<20)&&declaration.refs==1);
        const auto recoveryQueries=pressureMemoryReads;bump.setMemoryPressure(false);recoveryReads=pressureMemoryReads-recoveryQueries;
        assert(!bump.memoryPressure&&bump.deferredSnapshots.snapshotCacheLimit()==(24u<<20)&&bump.deferredGPU.limit()==(32u<<20));
        assert(DisplacementStateTestAccess::pressureLimit(bump.displacement)==(24u<<20)&&bump.deferredSnapshots.sharedSourceLimit()==(80u<<20));
        assert(!bump.deferredSnapshots.sharedSourceBytes()&&declaration.refs==1&&held->streams[0].bytes.back()==1);
        held.reset();assert(!bump.deferredSnapshots.sourceLiveObjects());
    }
    assert(NorthlightSourceIdentity::cpuLiveBytes.load()==initialCpu&&declaration.refs==1);
    {
        NorthlightAutoDisplacement geometry(&device);PressureDeclaration ownDeclaration;
        std::shared_ptr<const NorthlightDrawSnapshot::Mesh> held[3];
        for(unsigned i=0;i<3;++i){auto& pool=DisplacementStateTestAccess::pressureSources(geometry,i);held[i]=NorthlightDrawSnapshot::SnapshotTestAccess::seed(pool,&ownDeclaration,i?3:5,i?1u<<20:4u<<20);pool.clearFrame();}
        failAllocationAfter=0;geometry.setMemoryPressure(true);assert(failAllocationAfter==0);failAllocationAfter=-1;
        for(unsigned i=0;i<3;++i){auto& pool=DisplacementStateTestAccess::pressureSources(geometry,i);geometryEvictions+=pool.maintenance().snapshotEvictions;
            assert(!pool.sharedSourceBytes()&&!pool.snapshotCacheEntries()&&pool.snapshotCacheLimit()==(i?1u<<20:6u<<20));
            assert(pool.sourceLiveObjects()==1&&held[i]->streams[0].bytes.front()==1&&held[i]->streams[0].bytes.back()==1);}
        assert(ownDeclaration.refs==1&&ownDeclaration.releases==11);
        geometry.setMemoryPressure(false);for(unsigned i=0;i<3;++i){auto& pool=DisplacementStateTestAccess::pressureSources(geometry,i);assert(pool.snapshotCacheLimit()==(i?4u<<20:24u<<20));held[i].reset();assert(!pool.sourceLiveObjects());}
    }
    assert(NorthlightSourceIdentity::cpuLiveBytes.load()==initialCpu);
#ifdef NORTHLIGHT_TEST_MATERIAL_PRESSURE_POLICY
    {
        NorthlightAutoBump bump(&device);
        bump.setMemoryPressurePolicy(true,false);
        assert(bump.memoryPressure&&bump.deferredGPU.limit()==(32u<<20));
        assert(bump.deferredPacketLimit==512&&bump.deferredByteLimit==(8u<<20));
        assert(bump.deferredSnapshots.snapshotCacheLimit()==(4u<<20)&&DisplacementStateTestAccess::pressureLimit(bump.displacement)==(6u<<20));
        bump.setMemoryPressurePolicy(true,false);assert(bump.deferredGPU.limit()==(32u<<20));
        bump.setMemoryPressurePolicy(true,true);assert(bump.memoryPressure&&!bump.deferredGPU.limit()&&!bump.deferredGPU.chargedBytes());
        assert(bump.deferredPacketLimit==64&&bump.deferredByteLimit==(1u<<20));
        bump.setMemoryPressurePolicy(true,true);assert(bump.deferredPacketLimit==64&&bump.deferredByteLimit==(1u<<20));
        bump.setMemoryPressurePolicy(true,false);assert(bump.deferredGPU.limit()==(32u<<20));
        assert(bump.deferredPacketLimit==512&&bump.deferredByteLimit==(8u<<20));
        bump.trimMemory();assert(!bump.deferredGPU.chargedBytes()&&bump.deferredGPU.limit()==(32u<<20));
        bump.setMemoryPressurePolicy(false,false);assert(!bump.memoryPressure&&bump.deferredGPU.limit()==(32u<<20));
        assert(bump.deferredPacketLimit==512&&bump.deferredByteLimit==(8u<<20));
        std::puts("MATERIAL_PRESSURE_POLICY_PASS demand_32 critical_0 recovery_32 reduced_cpu_pools repeated trim");
    }
#endif
    std::printf("MATERIAL_PRESSURE_PASS before=%zu after=%zu shrinkEvictions=%u geometryShrinkEvictions=%u pressureMemoryReads=%u trimMemoryReads=%u recoveryMemoryReads=%u capMiB=42 allocationFree=1 heldBytesExact=1 declarationThread=1 predictionCleared=1 recovery=1\n",before,after,evictions,geometryEvictions,pressureReads,trimReads,recoveryReads);
}
int main(){MaterialCaptureStateTestAccess::pressureTransition();}
