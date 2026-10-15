#pragma once
#include "static_shadow_scene.h"
#include "static_shadow_dedup.h"
#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

// A rigid M2 doodad the client draws at its resting pose is also in the static shadow cache (a
// category 1/3 placement). The live replay then casts the same shadow a second time, in every
// cascade. This header holds the portable half of the decision that drops the live replay:
//  - decide(): every condition that must hold, in the order they are reported.
//  - covers(): the model identity proof. Every triangle of the replay, in world space, coincides
//    (VertexTolerance, any order and winding) with a triangle of the placement's OPAQUE batches.
//    Fail closed: anything unclear (or over the work/size caps) keeps the replay. Models above maxModelVertices
//    indices (4096 triangles by default) are not verified.
// The placement match (origin 0.05 yd, axes 0.01), the cache residency and the cascade's cached
// matrix test are the renderer's (world_renderer.h prepareStaticM2, replay loop).
namespace StaticM2Dedup {
using NorthlightGI::Vec3;
constexpr bool Enabled=true;
constexpr unsigned StableFrames=8;       // capture frames the bone's world matrix must not have moved
// Image neutrality: the skipped replay must put the same texels in the cascade as the cache does. A near-cascade texel is
// ~0.094 yd, so the tolerance has to stay far below it (2.5e-3 is ~2.7% of a texel, a sub-texel shift of the shadow edge
// is not representable). It also has to clear the float round-trip noise of one rest pose (world matrix -> skinned
// vertex vs. placement -> vertex): measured at most ~0.00195 yd at Stormwind coordinates. 2.5e-3 sits ~1.3x above that.
constexpr float VertexTolerance=2.5e-3f; // yards; triangle coincidence (covers)
constexpr float WorldTolerance=2.5e-3f;  // yards; the same budget for the bone's world translation (sameWorld)
// Coarse pre-filter ONLY. Geometry is proven by covers() (every submitted triangle coincides with a triangle of the
// placement within VertexTolerance); the box test merely rejects obviously wrong candidates cheaply and tells the cache
// record which region to check. Nothing relies on the pad to make two meshes "the same".
constexpr float BoundsPad=.25f;

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
// The replay's world-space TRIANGLES against the placement's opaque batches (see the header): every submitted
// triangle (its three world-space vertices, any order and winding, each within `tolerance`) must be a triangle
// of those batches. Vertex coincidence alone is not proof: the same vertex set can form other triangles.
// Opaque triangles are sorted by centroid x, so a submitted triangle only meets those within the tolerance window.
template<class P> bool covers(const StaticShadow::Model& model,const StaticShadow::Placement& place,const std::vector<P>& world,const std::vector<uint32_t>& indices,
    bool indexed,unsigned triangles,float tolerance=VertexTolerance,size_t maxModelVertices=12288,size_t maxTriangles=2048,size_t maxWork=1u<<20){
    using Tri=StaticShadowDedup::Triangle;
    if(world.empty()||!triangles||triangles>maxTriangles)return false;
    std::vector<Tri> submitted;if(!StaticShadowDedup::makeTriangles(world,indices,indexed,triangles,false,submitted,maxTriangles))return false;
    struct Opaque {Tri t;float cx,cy,cz;};
    std::vector<Opaque> opaque;size_t used=0;
    for(const auto& b:model.batches){
        if(b.material>=model.materials.size()||model.materials[b.material].alphaCutoff>0)continue;
        if(b.indexCount%3||b.firstIndex>model.indexCount()||b.indexCount>model.indexCount()-b.firstIndex)return false;
        used+=b.indexCount;if(used>maxModelVertices)return false;
        for(size_t i=b.firstIndex;i<size_t(b.firstIndex)+b.indexCount;i+=3){Opaque o;
            for(unsigned k=0;k<3;++k){const uint32_t v=model.indexAt(i+k);if(v>=model.vertices.size())return false;
                const auto& q=model.vertices[v];o.t[k]=StaticShadow::transformPoint(place,Vec3(q.x,q.y,q.z));}
            o.cx=(o.t[0].x+o.t[1].x+o.t[2].x)/3;o.cy=(o.t[0].y+o.t[1].y+o.t[2].y)/3;o.cz=(o.t[0].z+o.t[1].z+o.t[2].z)/3;opaque.push_back(o);}}
    if(opaque.empty())return false;
    size_t work=0; /* candidate comparisons: a hard bound on the call, over it the replay is kept */
    std::sort(opaque.begin(),opaque.end(),[](const Opaque& a,const Opaque& b){return a.cx<b.cx;});
    auto within=[&](const Vec3& a,const Vec3& b){return std::fabs(a.x-b.x)<=tolerance&&std::fabs(a.y-b.y)<=tolerance&&std::fabs(a.z-b.z)<=tolerance;};
    static const unsigned perm[6][3]={{0,1,2},{0,2,1},{1,0,2},{1,2,0},{2,0,1},{2,1,0}};
    for(const auto& t:submitted){
        const float cx=(t[0].x+t[1].x+t[2].x)/3,cy=(t[0].y+t[1].y+t[2].y)/3,cz=(t[0].z+t[1].z+t[2].z)/3;bool found=false;
        for(auto it=std::lower_bound(opaque.begin(),opaque.end(),cx-tolerance,[](const Opaque& o,float x){return o.cx<x;});it!=opaque.end()&&it->cx<=cx+tolerance&&!found;++it){
            if(++work>maxWork)return false;
            if(std::fabs(it->cy-cy)>tolerance||std::fabs(it->cz-cz)>tolerance)continue;
            for(const auto& p:perm)if(within(t[0],it->t[p[0]])&&within(t[1],it->t[p[1]])&&within(t[2],it->t[p[2]])){found=true;break;}}
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
    bool linked=false;
    uint32_t bucket=0; /* Memory: the chain this entry is linked in */
    size_t placementIndex=0;StaticShadow::Placement placement;unsigned long long revision=0;Vec3 low,high;
};
// Compared against the FIRST-SEEN matrix of an identity (Entry::world is never updated), so float noise within the
// tolerance neither creates a new identity nor chains drift frame to frame: a slow real drift leaves the tolerance
// once and becomes a new identity (stable restarts), noise never resets the count.
inline bool sameWorld(const float* a,const float* b){
    float scale=1;for(unsigned k=0;k<9;++k)scale=std::max(scale,std::fabs(a[k]));
    for(unsigned k=0;k<9;++k)if(!(std::fabs(a[k]-b[k])<=1e-4f*scale))return false;
    for(unsigned k=9;k<12;++k)if(!(std::fabs(a[k]-b[k])<=WorldTolerance))return false;
    return true;
}
// Fixed storage, hash-indexed by the identity AND the world translation quantized to CellSize cells. A query looks up
// every cell the tolerance window around its translation touches (usually one, up to eight at a cell corner), so a
// matrix that sameWorld accepts is always found whichever side of a cell boundary it falls. Many instances of one model
// and the stale matrices of a moving rigid draw therefore spread over buckets instead of one chain. Any chain that is
// walked unlinks entries not seen this or the previous frame (they cannot continue a stability run); every touch also
// sweeps a few slots so unvisited chains age out. Freed slots are recycled; entries seen this frame are never evicted.
// A full table (every entry seen this frame) hands out a scratch entry that never reaches the stable count, so it is
// never skipped. The storage is allocated once and references stay valid (no reallocation).
class Memory {
    static constexpr uint32_t None=0xffffffffu;
    static constexpr double CellSize=.25;
    static constexpr unsigned SweepPerTouch=2;
    std::vector<Entry> entries_;std::vector<uint32_t> heads_,next_,free_;uint32_t hand_=0,sweep_=0;Entry scratch_;
    unsigned long long fullFrame_=0,probes_=0,touches_=0;
    static long long cellOf(double v){if(!std::isfinite(v))return 0;v=std::floor(v/CellSize);return (long long)std::max(-1e9,std::min(1e9,v));}
    static size_t hashOf(const void* owner,const void* shader,const void* decl,unsigned start,unsigned count,long long cx,long long cy,long long cz){
        unsigned long long h=1469598103934665603ull;
        for(unsigned long long v:{(unsigned long long)(uintptr_t)owner,(unsigned long long)(uintptr_t)shader,(unsigned long long)(uintptr_t)decl,(unsigned long long)start,(unsigned long long)count,(unsigned long long)cx,(unsigned long long)cy,(unsigned long long)cz}){h^=v+0x9e3779b97f4a7c15ull+(h<<6)+(h>>2);h*=1099511628211ull;}
        return size_t(h^(h>>29));}
    void init(){if(!heads_.empty())return;entries_.reserve(Capacity);next_.reserve(Capacity);free_.reserve(Capacity);heads_.assign(Buckets,None);}
    void release(uint32_t i){auto& e=entries_[i];e.owner.reset();e.linked=false;e.state=Entry::Unverified;free_.push_back(i);}
    void unlink(uint32_t i){uint32_t* link=&heads_[entries_[i].bucket];while(*link!=None&&*link!=i)link=&next_[*link];if(*link==i)*link=next_[i];}
    void sweep(unsigned long long frame){
        for(unsigned n=0;n<SweepPerTouch&&!entries_.empty();++n){const uint32_t i=sweep_;sweep_=uint32_t((sweep_+1)%entries_.size());
            auto& e=entries_[i];if(e.linked&&e.frame+1<frame){unlink(i);release(i);}}}
public:
    static constexpr size_t Capacity=4096,Buckets=8192; /* Buckets: a power of two */
    void clear(){entries_.clear();next_.clear();free_.clear();if(!heads_.empty())heads_.assign(Buckets,None);hand_=sweep_=0;fullFrame_=probes_=touches_=0;}
    size_t size()const{return entries_.size()-free_.size();}
    unsigned long long probes()const{return probes_;}   /* chain entries visited so far (a cost measure for tests) */
    unsigned long long touches()const{return touches_;}
    // The entry of this identity at this frame; a moved matrix is a new identity (the old entry ages out).
    // stable counts the frames the identity was seen at the same matrix, once per frame.
    Entry& touch(const std::shared_ptr<const void>& owner,const void* shader,const void* decl,unsigned start,unsigned count,const float* world,unsigned long long frame){
        init();++touches_;sweep(frame);
        long long lo[3],hi[3],home[3];
        for(unsigned k=0;k<3;++k){const double v=world[9+k];lo[k]=cellOf(v-WorldTolerance);hi[k]=cellOf(v+WorldTolerance);home[k]=cellOf(v);}
        for(long long cx=lo[0];cx<=hi[0];++cx)for(long long cy=lo[1];cy<=hi[1];++cy)for(long long cz=lo[2];cz<=hi[2];++cz){
            const size_t at=hashOf(owner.get(),shader,decl,start,count,cx,cy,cz)&(Buckets-1);
            uint32_t* link=&heads_[at];
            while(*link!=None){const uint32_t i=*link;auto& e=entries_[i];++probes_;
                if(e.frame+1<frame){*link=next_[i];release(i);continue;} /* not seen this or last frame: cannot continue a run */
                if(!e.owner.owner_before(owner)&&!owner.owner_before(e.owner)&&e.shader==shader&&e.decl==decl&&e.start==start&&e.count==count&&sameWorld(e.world,world)){
                    if(e.frame!=frame){if(e.frame+1==frame)++e.stable;else e.stable=0;e.frame=frame;}
                    return e;}
                link=&next_[i];}}
        const size_t at=hashOf(owner.get(),shader,decl,start,count,home[0],home[1],home[2])&(Buckets-1);
        Entry fresh;fresh.bucket=uint32_t(at);fresh.owner=owner;fresh.shader=shader;fresh.decl=decl;fresh.start=start;fresh.count=count;fresh.frame=frame;fresh.linked=true;
        for(unsigned k=0;k<12;++k)fresh.world[k]=world[k];
        uint32_t i=None;
        if(!free_.empty()){i=free_.back();free_.pop_back();}
        else if(entries_.size()<Capacity){entries_.emplace_back();next_.push_back(None);i=uint32_t(entries_.size()-1);}
        else if(fullFrame_!=frame){
            for(size_t tried=0;tried<Capacity;++tried){
                const uint32_t j=hand_;hand_=(hand_+1)%Capacity;if(entries_[j].frame==frame)continue; /* seen this frame: keep */
                unlink(j);i=j;break;}
            if(i==None)fullFrame_=frame; /* every entry was seen this frame: no more scans until the next */
        }
        if(i==None){scratch_=std::move(fresh);scratch_.stable=0;scratch_.state=Entry::Rejected;return scratch_;}
        entries_[i]=std::move(fresh);next_[i]=heads_[at];heads_[at]=i;return entries_[i];
    }
};
} // namespace StaticM2Dedup
