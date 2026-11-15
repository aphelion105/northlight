#pragma once
#include "world_probe_progress.h"
#include "probe_local_visibility.h"

namespace NorthlightGI {
// A second static cascade fills idle solve time. It owns separate keys/cache;
// actor corrections and full ray counts remain exclusive to the near grid.
inline constexpr unsigned ProbeCascadeCount=2;
inline constexpr float CoarseProbeScale=4.f;
inline bool coarseProbeSupported(Vec3 p,Vec3 geometryCenter,float radius){
    const Vec3 d=p-geometryCenter;
    // Keep the entire local visibility cell inside the loaded geometry crop.
    return std::fabs(d.x)+64<=radius&&std::fabs(d.y)+64<=radius&&std::fabs(d.z)+64<=320;
}
inline Probe solveCoarseProbe(const BVH& bvh,ProbeGridKey key,Vec3 position,
                             const PreparedLighting& light,unsigned rays){
    auto value=solveProbePrepared(bvh,position,light,std::max(8u,rays/4),probeSeed(key)^0x9e3779b9u);
    if(value.valid)value.localVisibility=LocalProbeVisibility::build(bvh,position,64);
    return value;
}
class CoarseProbeLayer {
    ProbeCache cache_;
    std::shared_ptr<const std::vector<ProbeAtlasEntry>> displayed_;
    std::string map_;
    uint64_t generation_=0;
    Vec3 completedOrigin_;
    bool complete_=false;
public:
    auto atlas(const std::string& map)const{return map==map_?displayed_:nullptr;}
    bool completeFor(Vec3 camera)const{
        const auto o=probeWindowOrigin(camera/CoarseProbeScale);
        return complete_&&o.x==completedOrigin_.x&&o.y==completedOrigin_.y&&o.z==completedOrigin_.z;
    }
    void accountMemory(NorthlightMemoryFootprint::Counter& memory)const{
        cache_.accountMemory(memory);
        if(displayed_&&memory.visit(displayed_.get()))accountProbeAtlas(memory,*displayed_);
    }
    template<class Cancel,class Publish,class Clock>
    void run(Vec3 camera,Vec3 center,float radius,const BVH& bvh,const PreparedLighting& light,
             unsigned rays,uint64_t generation,const std::string& map,Cancel cancel,Publish publish,Clock clock){
        if(generation_!=generation||map_!=map){
            cache_.clear();complete_=false;generation_=generation;
            if(map_!=map)displayed_.reset();map_=map;
        }
        if(completeFor(camera))return;
        const auto origin=probeWindowOrigin(camera/CoarseProbeScale);
        const auto order=probeSolveOrder(camera/CoarseProbeScale);
        uint32_t last=clock();bool dirty=false;
        auto flush=[&]{
            if(!dirty)return;
            auto next=std::make_shared<std::vector<ProbeAtlasEntry>>(cache_.atlas());
            if(displayed_)retainProbeDisplayFallback(*next,*displayed_);
            displayed_=std::move(next);dirty=false;last=clock();publish(displayed_);
        };
        for(unsigned i:order){
            // One bounded probe at a time; any new near request preempts this pass.
            if(cancel()){flush();return;}
            const auto q=probeWindowPosition(origin,i),p=q*CoarseProbeScale;
            ProbeGridKey key;if(!probeGridKey(q,key))continue;
            if(!coarseProbeSupported(p,center,radius)||cache_.contains(key))continue;
            cache_.putMissing(key,solveCoarseProbe(bvh,key,p,light,rays));dirty=true;
            if(uint32_t(clock()-last)>=100)flush();
        }
        flush();completedOrigin_=origin;complete_=true;
    }
};
}
