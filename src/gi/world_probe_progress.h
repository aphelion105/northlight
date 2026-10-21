#pragma once
#include "world_probe_cache.h"
#include <vector>

namespace NorthlightGI {
// Solve the camera's eight interpolation corners first, then expand outwards.
// This changes scheduling only: every point in the window (probeLayout()) keeps
// its exact world key, ray seed and full transport solve.
inline Vec3 probeWindowOrigin(Vec3 camera) {
    const auto& l=probeLayout();
    const float offset=float(l.n/2-1)*8,offsetZ=float(l.nz/2-1)*8;
    return {std::floor(camera.x/8)*8-offset,std::floor(camera.y/8)*8-offset,
            std::floor(camera.z/8)*8-offsetZ};
}
inline Vec3 probeWindowPosition(Vec3 origin,unsigned index) {
    const unsigned g=probeLayout().n;
    return origin+Vec3(float(index%g)*8,float((index/g)%g)*8,float(index/(g*g))*8);
}
inline std::vector<unsigned> probeSolveOrder(Vec3 camera) {
    struct Item {unsigned index;float distance;bool corner;};
    const auto& l=probeLayout();const unsigned count=l.count();
    std::vector<Item> items(count);
    const Vec3 origin=probeWindowOrigin(camera);
    const unsigned g=l.n,base=g/2-1,baseZ=l.nz/2-1;
    for(unsigned i=0;i<count;++i) {
        unsigned x=i%g,y=(i/g)%g,z=i/(g*g);
        Vec3 delta=probeWindowPosition(origin,i)-camera;
        items[i]={i,dot(delta,delta),x>=base&&x<=base+1&&y>=base&&y<=base+1&&z>=baseZ&&z<=baseZ+1};
    }
    std::sort(items.begin(),items.end(),[](const Item& a,const Item& b){
        if(a.corner!=b.corner)return a.corner;
        if(a.distance!=b.distance)return a.distance<b.distance;
        return a.index<b.index;
    });
    std::vector<unsigned> order(count);
    for(unsigned i=0;i<count;++i)order[i]=items[i].index;
    return order;
}

// Display continuity only. The old atlas never enters the new generation's
// solve cache, and moving corrections must be applied before this merge.
// Occupied invalid results replace old values too; absence is not invalidity.
inline void retainProbeDisplayFallback(std::vector<ProbeAtlasEntry>& current,
                                       const std::vector<ProbeAtlasEntry>& previous) {
    const size_t size=probeLayout().atlasSize();
    if(current.size()!=size||previous.size()!=size)return;
    for(size_t i=0;i<size;++i)if(!current[i].occupied)current[i]=previous[i];
}

// Worker-owned and shared across camera requests in one static generation.
// Camera motion must not restart this clock and postpone publication indefinitely.
// Completion still publishes immediately using the normal final path.
class ProbePublicationCadence {
    uint32_t last_=0;
    bool published_=false,dirty_=false;
public:
    // Publish during camera motion: 250 ms was thirty frames at 120 Hz.
    static constexpr uint32_t IntervalMs=33;
    void reset(){last_=0;published_=dirty_=false;}
    void solved(){dirty_=true;}
    bool due(uint32_t now,unsigned processed)const {
        return dirty_&&processed>=8&&(!published_||uint32_t(now-last_)>=IntervalMs);
    }
    void published(uint32_t now){last_=now;published_=true;dirty_=false;}
};
} // namespace NorthlightGI
