#pragma once
#include "static_shadow_scene.h"
#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

// A rigid M2 doodad the client draws at its resting pose is also in the static shadow cache (a
// category 1/3 placement). The live replay then casts the same shadow a second time, in every
// cascade. This header holds the portable half of the decision that drops the live replay:
//  - decide(): every condition that must hold, in the order they are reported.
//  - covers(): the model identity proof. Every vertex of the replay, in world space, coincides
//    (VertexTolerance) with a vertex of the placement's OPAQUE batches, and the replay has no more
//    triangles than those batches. Fail-open: anything unclear keeps the replay.
// The placement match (origin 0.05 yd, axes 0.01), the cache residency and the cascade's cached
// matrix test are the renderer's (world_renderer.h prepareStaticM2, replay loop).
namespace StaticM2Dedup {
using NorthlightGI::Vec3;
constexpr bool Enabled=true;
constexpr unsigned StableFrames=8;       // capture frames the bone's world matrix must not have moved
constexpr float VertexTolerance=.02f;    // yards; float noise of two evaluations of one bindpose
constexpr float BoundsPad=.25f;          // the replay's bounds may exceed the placement's box by this
enum class Verdict : unsigned char {Skip,Skinned,Unplaced,ModelMismatch,Animated,CacheInvalid,BoundsOutside};
struct Facts {
    bool rigid=false;          // one bone, an audited palette program, opaque
    bool placed=false;         // the bone's world matrix is a static-cache placement
    bool modelMatches=false;   // covers() held for that placement's model
    bool cacheValid=false;     // the placement's model is complete and resident in the slot cache
    bool boundsInside=false;   // the replay's bounds lie inside the placement's box
    unsigned stableFrames=0;   // consecutive capture frames the world matrix was unchanged
};
inline Verdict decide(const Facts& f){
    if(!f.rigid)return Verdict::Skinned;
    if(!f.placed)return Verdict::Unplaced;
    if(!f.modelMatches)return Verdict::ModelMismatch;
    if(f.stableFrames<StableFrames)return Verdict::Animated;
    if(!f.cacheValid)return Verdict::CacheInvalid;
    if(!f.boundsInside)return Verdict::BoundsOutside;
    return Verdict::Skip;
}
inline const char* name(Verdict v){
    static const char* n[]={"skip","skinned","unplaced","modelMismatch","animated","cacheInvalid","boundsOutside"};return n[unsigned(v)];}
// Bounds of the world positions; false when empty or not finite.
template<class P> bool boundsOf(const std::vector<P>& world,Vec3& low,Vec3& high){
    if(world.empty())return false;
    low=Vec3(INFINITY,INFINITY,INFINITY);high=Vec3(-INFINITY,-INFINITY,-INFINITY);
    for(const auto& p:world){if(!std::isfinite(p.x)||!std::isfinite(p.y)||!std::isfinite(p.z))return false;
        low.x=std::min<float>(low.x,p.x);low.y=std::min<float>(low.y,p.y);low.z=std::min<float>(low.z,p.z);
        high.x=std::max<float>(high.x,p.x);high.y=std::max<float>(high.y,p.y);high.z=std::max<float>(high.z,p.z);}
    return true;
}
inline bool inside(const StaticShadow::Placement& place,Vec3 low,Vec3 high,float pad=BoundsPad){
    return low.x>=place.low.x-pad&&low.y>=place.low.y-pad&&low.z>=place.low.z-pad&&high.x<=place.high.x+pad&&high.y<=place.high.y+pad&&high.z<=place.high.z+pad;
}
// The replay's world-space vertices against the placement's opaque batches (see the header).
template<class P> bool covers(const StaticShadow::Model& model,const StaticShadow::Placement& place,const std::vector<P>& world,unsigned triangles,
    float tolerance=VertexTolerance,size_t maxModelVertices=65536){
    if(world.empty()||!triangles)return false;
    std::vector<Vec3> points;size_t opaque=0;
    for(const auto& b:model.batches){
        if(b.material>=model.materials.size()||model.materials[b.material].alphaCutoff>0)continue;
        if(b.indexCount%3||b.firstIndex>model.indexCount()||b.indexCount>model.indexCount()-b.firstIndex)return false;
        opaque+=b.indexCount/3;if(points.size()+b.indexCount>maxModelVertices)return false;
        for(size_t i=b.firstIndex;i<size_t(b.firstIndex)+b.indexCount;++i){const uint32_t v=model.indexAt(i);if(v>=model.vertices.size())return false;
            const auto& q=model.vertices[v];points.push_back(StaticShadow::transformPoint(place,Vec3(q.x,q.y,q.z)));}}
    if(!opaque||triangles>opaque)return false;
    std::sort(points.begin(),points.end(),[](const Vec3& a,const Vec3& b){return a.x<b.x;});
    for(const auto& w:world){
        if(!std::isfinite(w.x)||!std::isfinite(w.y)||!std::isfinite(w.z))return false;
        bool found=false;
        for(auto it=std::lower_bound(points.begin(),points.end(),w.x-tolerance,[](const Vec3& a,float x){return a.x<x;});it!=points.end()&&it->x<=w.x+tolerance;++it)
            if(std::fabs(it->y-w.y)<=tolerance&&std::fabs(it->z-w.z)<=tolerance){found=true;break;}
        if(!found)return false;}
    return true;
}
// Per-identity memory across capture frames: the bone's world matrix, how long it has not moved and
// the one-time verification. An identity is the shared snapshot, the program, the declaration, the
// range and the world matrix (identical models at two places share a snapshot).
struct Entry {
    std::weak_ptr<const void> owner; /* the control block stays allocated while the entry holds it: an address is never reused for another mesh */
    const void* shader=nullptr,*decl=nullptr;unsigned start=0,count=0;
    float world[12]={};unsigned stable=0;unsigned long long frame=0,sceneRevision=0;
    enum State : unsigned char {Unverified,Matched,Rejected} state=Unverified;
    size_t placementIndex=0;StaticShadow::Placement placement;unsigned long long revision=0;Vec3 low,high;
};
inline bool sameWorld(const float* a,const float* b){
    float scale=1;for(unsigned k=0;k<9;++k)scale=std::max(scale,std::fabs(a[k]));
    for(unsigned k=0;k<9;++k)if(!(std::fabs(a[k]-b[k])<=1e-4f*scale))return false;
    for(unsigned k=9;k<12;++k)if(!(std::fabs(a[k]-b[k])<=1e-3f))return false;
    return true;
}
class Memory {
    std::vector<Entry> entries_;
public:
    static constexpr size_t Capacity=256;
    void clear(){entries_.clear();}
    size_t size()const{return entries_.size();}
    // The entry of this identity at this frame; a moved matrix is a new identity (the old entry ages out).
    // stable counts the frames the identity was seen at the same matrix, once per frame.
    Entry& touch(const std::shared_ptr<const void>& owner,const void* shader,const void* decl,unsigned start,unsigned count,const float* world,unsigned long long frame){
        for(auto& e:entries_)if(!e.owner.owner_before(owner)&&!owner.owner_before(e.owner)&&e.shader==shader&&e.decl==decl&&e.start==start&&e.count==count&&sameWorld(e.world,world)){
            if(e.frame!=frame){if(e.frame+1==frame)++e.stable;else e.stable=0;e.frame=frame;}
            return e;}
        Entry fresh;fresh.owner=owner;fresh.shader=shader;fresh.decl=decl;fresh.start=start;fresh.count=count;fresh.frame=frame;
        for(unsigned k=0;k<12;++k)fresh.world[k]=world[k];
        if(entries_.size()<Capacity){entries_.push_back(std::move(fresh));return entries_.back();}
        auto oldest=std::min_element(entries_.begin(),entries_.end(),[](const Entry& a,const Entry& b){return a.frame<b.frame;});
        *oldest=std::move(fresh);return *oldest;
    }
};
} // namespace StaticM2Dedup
