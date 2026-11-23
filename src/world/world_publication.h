#pragma once
#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>

namespace NorthlightWorldPublication {
// CPU input and output shared by the render thread and the two workers.
class State {
protected:
    using V=NorthlightGI::Vec3;
    /* 0.3.137: render-thread commit data built by the GI worker from the same
       immutable plan (see PreparedCommitData). Null fields fall back to the
       0.3.136 render-thread copies. */
    using FixedChunks=std::set<std::pair<int,int>>;
    struct PreparedCommit {const void* plan=nullptr; /* identity of the source plan; checked at commit */
        std::shared_ptr<const StaticShadow::OwnerSet> owners;std::shared_ptr<const FixedChunks> fixed;};
    static constexpr bool PreparedCommitData=StaticShadow::PreparedCoveredOwners;
    struct Snapshot {
        std::shared_ptr<NorthlightGI::BVH> bvh;
        std::shared_ptr<NorthlightWorldMesh::WorldMeshUploadPlan> meshPlan;
        std::vector<NorthlightGI::ProbeAtlasEntry> atlas;
        std::shared_ptr<const std::vector<NorthlightGI::ProbeAtlasEntry>> coarseAtlas;
        std::vector<NorthlightLocalLights::Light> localLights;
        std::shared_ptr<const NorthlightLightAttachments::Records> lightAttachments;
        V origin, center;
        std::string map, message;
        uint64_t serial=0;
        std::shared_ptr<const NorthlightRegionalFog::Field> fogField;
        unsigned validProbes=0;
        uint64_t requestId=0,requestActorSerial=0;
        DWORD requestedAt=0,queueMs=0,geometryMs=0,actorMs=0,solveMs=0;
        double actorWorkMs=0,solveWorkMs=0;
        double actorDecodeMs=0,actorPoseMs=0,actorHashMs=0,actorBvhMs=0;
        unsigned actorTexturesDecoded=0;size_t actorTextureEncodedBytes=0;
        size_t retirementBytes=0;
        std::shared_ptr<const PreparedCommit> prepared; /* paired with meshPlan */
        unsigned reusedProbes=0,solvedProbes=0,superseded=0,dynamicReused=0,dynamicSolved=0;
        unsigned processedProbes=0,retargeted=0;
        uint64_t lightingGeneration=0;
        struct ProbeRadiance {V sunDirection,sunIrradiance,skyRadiance;bool known=false;} probeRadiance;
        NorthlightGI::MovingSolveStats movingStats;size_t pathRecordBytes=0;
        NorthlightGI::ProbeRelightCache::Stats relightStats;
        NorthlightGI::ProbeVisibilityCache::Stats visibilityStats;
        bool staticOnly=false,partial=false;
    };
    // probeCenter (0.3.153 GIProbeAhead): probe window origin, solve order and move trigger; geometryCenter (orbit pivot + bounded travel lead):
    // the region build centre and its refresh trigger; camera (eye) for everything else, including every coverage check.
    struct Request { std::shared_ptr<const NorthlightActorGeometry::ActorJob> actorJob;uint64_t actorSerial=0; V camera,probeCenter,geometryCenter; NorthlightGI::Lighting light; std::string map; uint64_t id=0,baseId=0;DWORD queuedAt=0;unsigned reason=0;bool enabled=true,giEnabled=true,fogEnabled=true,auroraActive=false,menu=false;NorthlightNativeLights::Snapshot menuLights; };
    std::thread worker;
    std::mutex mutex;
    std::condition_variable wake;
    std::condition_variable giSettled;
    bool stopping=false,pending=false;
    std::atomic<bool> workerBusy{false};
    std::atomic<unsigned> workerFaultCode{0};
    Request request,lastRequest;
    std::shared_ptr<Snapshot> published,active;
    std::shared_ptr<const Snapshot> payloadRebuildSource;
    bool geometryRebuild=false;

    // Both copies stay under the same lock as advancement of request IDs.
    void queueRequestLocked(Request& next,DWORD now,bool newBase){
        next.id=request.id+1;
        if(newBase)next.baseId=next.id;
        next.queuedAt=now;
        request=next;
        lastRequest=next;
        pending=true;
    }
    void setWorkerEnabled(bool value){
        {std::lock_guard<std::mutex> lock(mutex);request.enabled=value;
         if(!value){request.giEnabled=false;++request.id;pending=true;}}
        if(!value)wake.notify_one();
    }
    void enqueuePayloadRebuild(){
        {std::lock_guard<std::mutex> lock(mutex);payloadRebuildSource=active;geometryRebuild=true;pending=true;}
        wake.notify_one();
    }
    void stopWorker(){
        {std::lock_guard<std::mutex> lock(mutex);stopping=true;}
        wake.notify_one();
        if(worker.joinable())worker.join();
    }
    template<class Built>
    bool takeRequest(Request& next,std::unique_ptr<Built>& built,std::unique_ptr<Built>& adopted,bool& geometryWanted){
        std::unique_lock<std::mutex> lock(mutex);
        wake.wait(lock,[&]{return stopping||pending||(request.enabled&&built);});
        if(stopping)return false;
        next=request;
        pending=false;
        workerBusy=true;
        if(next.enabled)adopted=std::move(built);
        if(next.enabled&&geometryRebuild){geometryRebuild=false;geometryWanted=true;}
        return true;
    }
    void finishRequest(bool fixed){
        if(!fixed){workerBusy=false;return;}
        {std::lock_guard<std::mutex> lock(mutex);workerBusy=false;}
        giSettled.notify_one();
    }
    void publishProbesLocked(std::shared_ptr<Snapshot> next,uint64_t& serial){
        next->serial=++serial;
        published=std::move(next);
    }
};
}
