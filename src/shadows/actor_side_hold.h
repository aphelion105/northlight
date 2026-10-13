#pragma once
#include "actor_shadow_map.h"
#include <cstdint>

// Which map a skinned caster is drawn into: the actor map (while its bounds lie inside the inset) or the near cascade. Without memory the
// inset test flips a caster whose bounds straddle the edge from frame to frame. Hold adds hysteresis: a caster enters only fully inside
// Inset (today's rule) and, once in, stays while its bounds lie within Stay (actor_shadow_map.h explains the margin against the sampling
// shader). Only casters in the actor map are remembered: one slot per caster in a fixed table (no allocation, no lock, O(1)); the table is sized for a crowd (Slots); a caster
// that is not remembered is in the near cascade.
// Identity: the caster's model (snapshot owner, declaration, index count: a hash chosen by the caller) and its position: the palette root,
// or, for a caster without one (a rigid attachment: weapon, shoulder; the caller takes the centre of its valid bounds, and mixes it into
// the model hash so the two kinds never match). Clones of one snapshot differ by position, so a slot matches only a position within Reach (a caster moves far less per frame); the table is a grid of 8 yd cells
// of the position, probed in the caster's own cell and in the neighbours when the root is within Reach of the cell border, so crossing a cell
// border never loses a caster. A remembered caster that is not seen as inside Stay for more than Gap frames is forgotten: leaving and
// coming back within the band must enter again, and a capture gap (one frame without the replay) keeps the state.
//
// Carry (missing bounds): the bounds are rebuilt every frame under a budget, so a caster that was in the actor map can lack them for a frame or
// two, and without them it falls to the near cascade. A caster whose last bounds lay deep inside the map (extent <= Deep, so 0.10 clip units =
// 1.2 yd short of Inset and 1.7 yd short of Stay) is kept in the actor map while all of this holds: (a) its last extent was <= Deep, (b) its position
// (palette root or rigid anchor) has not moved in the map's own frame by more than EpsClip (0.02 clip = 0.24 yd in light-space x/y and depth,
// which includes the map centre's own motion: the positions are compared as clip coordinates of each frame's matrix), so the estimate
// extent + EpsClip stays <= Deep + EpsClip = .72, (c) those bounds are at most CarryAge (2) real frames old (begin() takes the renderer's frame count; a pass that did not run on a frame makes the marks older, so a run after a gap never carries on stale bounds). Same identity as the hold (model hash,
// position), found through a direct mapped table of 1 yd map-space cells, written by every skinned caster that is deep in the map (stamp) and
// read only for a caster without bounds (carry). The one assumption that bounds the risk: with the position fixed, the caster's pose does not
// grow by more than ~1.2 yd toward the map edge within 2 frames (a limb would have to move 36 yd/s toward the edge); beyond that part of its
// shadow would reach the shader's fade (.88) while carried, at most for those 2 frames. Not covered by a proof: two clones of one model stacked
// within EpsClip of each other (the younger mark is used), and a pose jump of the same snapshot (a teleport or mount change moves the position or
// changes the model hash, so they do not match). A mark whose frame equals the current one belongs to a caster that has bounds this frame and is
// never matched. Collisions overwrite (the other caster merely loses its carry).
namespace NorthlightActorSideHold {
inline constexpr float Reach=1.f,Cell=8.f,Deep=NorthlightActorShadowMap::Inset-.10f;
// Sizing: only the casters in the strip (Deep, Inset] and in the band to Stay are looked up and remembered, but a crowd puts 100-300 skinned
// draws in the actor map (the diagnostics' tracked counts) and a third of them can be in that strip at once; the clones of one character
// model (equal model hash) that stand in one 8 yd cell share one chain. 1024 slots keep the load under a fifth with 200 casters in the
// strip, and a chain of 8 holds eight clones of a model in one cell and absorbs chains that run into each other (200 casters of 12 models over
// 24 x 24 yd: 1 not remembered, against 256 slots in chains of 4, which overflowed in game crowds). 24 KB, fixed: no allocation. A caster
// deeper than Deep never touches the table, so the working set is a few cache lines per frame.
inline constexpr unsigned Slots=1024,Probes=8,Gap=2;
inline constexpr float EpsClip=.02f;inline constexpr unsigned Marks=1024,CarryAge=2; /* 24 KB */
static_assert(Deep+EpsClip<=NorthlightActorShadowMap::Inset,"a carried caster stays inside the inset");
struct Mark {std::uint32_t check=0,frame=0;float clip[3]={};float extent=0;}; /* frame == 0: free */
struct Slot {std::uint64_t model=0;float root[3]={};unsigned seen=0;}; /* seen == 0: free (frame_ is never 0) */
static_assert((Slots&(Slots-1))==0,"a power of two: the slot index is masked");
class Hold {
    Slot slots_[Slots];Mark marks_[Marks];unsigned frame_=1,real_=1,floor_=0; /* real_: the caller's frame count (marks age in real frames, not in runs of the pass); floor_: marks at or below it are void */bool diagnostics_=false;
    static std::uint32_t cell(float v){return std::uint32_t(std::int32_t(std::floor(v/Cell)));}
    static std::uint32_t slotOf(std::uint64_t model,std::uint32_t cx,std::uint32_t cy){
        std::uint64_t h=model^(std::uint64_t(cx)*0x9E3779B97F4A7C15ull)^(std::uint64_t(cy)*0xC2B2AE3D27D4EB4Full);h^=h>>29;h*=0xBF58476D1CE4E5B9ull;h^=h>>32;return std::uint32_t(h)&(Slots-1);}
    static bool clipOf(const float* m,const float* at,float* c){
        for(unsigned a=0;a<3;++a)c[a]=at[0]*m[a]+at[1]*m[4+a]+at[2]*m[8+a]+m[12+a];
        return std::isfinite(c[0])&&std::isfinite(c[1])&&std::isfinite(c[2]);}
    static std::uint64_t markHash(std::uint64_t model,std::int32_t cx,std::int32_t cy){
        std::uint64_t h=model^(std::uint64_t(std::uint32_t(cx))*0x9E3779B97F4A7C15ull)^(std::uint64_t(std::uint32_t(cy))*0xC2B2AE3D27D4EB4Full);h^=h>>29;h*=0xBF58476D1CE4E5B9ull;h^=h>>32;return h;}
    static std::int32_t markCell(float clip){return std::int32_t(std::floor(clip*NorthlightActorShadowMap::Radius));} /* 1 yd of map space */
    bool usable(const Slot& s)const{return s.seen&&frame_-s.seen<=Gap;}
    void advance(unsigned n){frame_+=n;if(!frame_)frame_=1;}
    Slot* find(std::uint64_t model,const float* root,unsigned* cellsProbed=nullptr){
        const std::int32_t bx=std::int32_t(cell(root[0])),by=std::int32_t(cell(root[1]));
        const float fx=root[0]-std::floor(root[0]/Cell)*Cell,fy=root[1]-std::floor(root[1]/Cell)*Cell;
        const int x0=fx<Reach?-1:0,x1=fx>Cell-Reach?1:0,y0=fy<Reach?-1:0,y1=fy>Cell-Reach?1:0;
        Slot* best=nullptr;float bestD=Reach*Reach;
        for(int dx=x0;dx<=x1;++dx)for(int dy=y0;dy<=y1;++dy){
            if(cellsProbed)++*cellsProbed;
            const std::uint32_t at=slotOf(model,std::uint32_t(bx+dx),std::uint32_t(by+dy));
            for(unsigned k=0;k<Probes;++k){Slot& s=slots_[(at+k)&(Slots-1)];
                if(!usable(s)||s.model!=model||s.seen==frame_)continue; /* seen this frame: another clone's */
                const float x=s.root[0]-root[0],y=s.root[1]-root[1],z=s.root[2]-root[2],d=x*x+y*y+z*z;
                if(d<=bestD){bestD=d;best=&s;}}}
        return best;
    }
    // A full chain means this caster is not remembered (it stays in the actor map this frame, and enters again later): counted in diagnostics.
    void remember(Slot* found,std::uint64_t model,const float* root){
        if(found){ /* same slot unless the root moved to another cell: its chain is looked up by the cell */
            if(cell(found->root[0])==cell(root[0])&&cell(found->root[1])==cell(root[1])){for(unsigned k=0;k<3;++k)found->root[k]=root[k];found->seen=frame_;return;}
            found->seen=0;}
        const std::uint32_t at=slotOf(model,cell(root[0]),cell(root[1]));
        for(unsigned k=0;k<Probes;++k){Slot& s=slots_[(at+k)&(Slots-1)];
            if(usable(s))continue;
            s.model=model;for(unsigned c=0;c<3;++c)s.root[c]=root[c];s.seen=frame_;return;}
        if(diagnostics_)++overflows;
    }
public:
    // Diagnostics only (counted when begin(true) opened the pass; no cost otherwise): overflows since the start (a full 4-slot chain: the
    // caster was not remembered), bandHeld in the last pass (casters kept in the actor map by the hold while beyond Inset).
    unsigned long overflows=0,carriedTotal=0;unsigned bandHeld=0,carried=0;
    // Once per pass, before the casters.
    void begin(bool diagnostics=false,unsigned realFrame=0){advance(1);real_=realFrame?realFrame:frame_;diagnostics_=diagnostics;bandHeld=0;carried=0;}
    void reset(){for(auto& s:slots_)s=Slot{};for(auto& k:marks_)k=Mark{};frame_=real_=1;floor_=0;}
    // The pass did not run (actor map off, a reset): every remembered caster expires at once, O(1).
    void invalidate(){advance(Gap+1);floor_=real_=real_>frame_?real_:frame_;}
    // extent: insetExtent(bounds, Stay) (infinity: invalid, beyond Stay or outside the depth planes). at: the caster's position (see above);
    // null: no identity, today's rule.
    // A caster deeper than Deep is in the actor map without a table access: the state only matters at the edge, and to reach the band from
    // Deep a caster must pass (Deep, Inset], where it is remembered. Deep is Inset - 1.2 yd (.10 of 12 yd): a caster would have to move
    // more than 1.2 yd in light space in one frame (72 yd/s at 60 fps) to skip the strip, and then it only goes to the near cascade for
    // that frame, as without the hold.
    bool decide(std::uint64_t model,const float* root,float extent){
        if(extent<=Deep)return true;
        if(!(extent<=NorthlightActorShadowMap::Stay))return false;
        if(!root||!std::isfinite(root[0])||!std::isfinite(root[1])||!std::isfinite(root[2]))return extent<=NorthlightActorShadowMap::Inset;
        Slot* found=find(model,root);
        const bool inside=extent<=NorthlightActorShadowMap::Inset;
        if(!inside&&!found)return false;
        if(diagnostics_&&!inside)++bandHeld;
        remember(found,model,root);
        return true;
    }
    // A skinned caster that is in the actor map with valid bounds, deep inside (extent <= Deep): remember its extent and where it is in the map's frame m.
    void stamp(std::uint64_t model,const float* at,const float* m,float extent){
        if(!(extent<=Deep))return;
        float c[3];if(!clipOf(m,at,c))return;
        const std::uint64_t h=markHash(model,markCell(c[0]),markCell(c[1]));Mark& k=marks_[std::uint32_t(h)&(Marks-1)];
        k.check=std::uint32_t(h>>32);k.frame=real_;k.extent=extent;for(unsigned a=0;a<3;++a)k.clip[a]=c[a];
    }
    // A skinned caster without valid bounds: true when it stays in the actor map (see Carry above).
    bool carry(std::uint64_t model,const float* at,const float* m){
        float c[3];if(!clipOf(m,at,c))return false;
        const float fx=c[0]*NorthlightActorShadowMap::Radius,fy=c[1]*NorthlightActorShadowMap::Radius,edge=EpsClip*NorthlightActorShadowMap::Radius;
        const std::int32_t bx=markCell(c[0]),by=markCell(c[1]);
        const float ox=fx-float(bx),oy=fy-float(by);
        const int x0=ox<edge?-1:0,x1=ox>1.f-edge?1:0,y0=oy<edge?-1:0,y1=oy>1.f-edge?1:0;
        const float depth=NorthlightWorldMath::ShadowDepthSpan; /* clip depth 1 = this many yards */
        for(int dx=x0;dx<=x1;++dx)for(int dy=y0;dy<=y1;++dy){
            const std::uint64_t h=markHash(model,bx+dx,by+dy);const Mark& k=marks_[std::uint32_t(h)&(Marks-1)];
            if(!k.frame||k.check!=std::uint32_t(h>>32)||k.frame<=floor_||k.frame==real_||real_-k.frame>CarryAge||!(k.extent<=Deep))continue;
            if(std::fabs(k.clip[0]-c[0])<=EpsClip&&std::fabs(k.clip[1]-c[1])<=EpsClip&&std::fabs(k.clip[2]-c[2])*depth<=EpsClip*NorthlightActorShadowMap::Radius){
                if(diagnostics_){++carried;++carriedTotal;}return true;}}
        return false;
    }
    unsigned remembered()const{unsigned n=0;for(const auto& s:slots_)n+=usable(s)&&s.seen==frame_;return n;}
};
}
