#pragma once
#include "actor_shadow_map.h"
#include <cstdint>

// Which map a skinned caster is drawn into: the actor map (while its bounds lie inside the inset) or the near cascade. Without memory the
// inset test flips a caster whose bounds straddle the edge from frame to frame. Hold adds hysteresis: a caster enters only fully inside
// Inset (today's rule) and, once in, stays while its bounds lie within Stay (actor_shadow_map.h explains the margin against the sampling
// shader). Only casters in the actor map are remembered: one slot per caster in a fixed table (no allocation, no lock, O(1)); a caster
// that is not remembered is in the near cascade.
// Identity: the caster's model (snapshot owner, declaration, index count: a hash chosen by the caller) and its position: the palette root,
// or, for a caster without one (a rigid attachment: weapon, shoulder; the caller takes the centre of its valid bounds, and mixes it into
// the model hash so the two kinds never match). Clones of one snapshot differ by position, so a slot matches only a position within Reach (a caster moves far less per frame); the table is a grid of 8 yd cells
// of the position, probed in the caster's own cell and in the neighbours when the root is within Reach of the cell border, so crossing a cell
// border never loses a caster. A remembered caster that is not seen as inside Stay for more than Gap frames is forgotten: leaving and
// coming back within the band must enter again, and a capture gap (one frame without the replay) keeps the state.
namespace NorthlightActorSideHold {
inline constexpr float Reach=1.f,Cell=8.f,Deep=NorthlightActorShadowMap::Inset-.10f;
inline constexpr unsigned Slots=256,Probes=4,Gap=2; /* 8 KB: the table stays in L1; chains of 4 slots lie in two cache lines */
struct Slot {std::uint64_t model=0;float root[3]={};unsigned seen=0;bool live=false;};
static_assert((Slots&(Slots-1))==0,"a power of two: the slot index is masked");
class Hold {
    Slot slots_[Slots];unsigned frame_=1;bool diagnostics_=false;
    static std::uint32_t cell(float v){return std::uint32_t(std::int32_t(std::floor(v/Cell)));}
    static std::uint32_t slotOf(std::uint64_t model,std::uint32_t cx,std::uint32_t cy){
        std::uint64_t h=model^(std::uint64_t(cx)*0x9E3779B97F4A7C15ull)^(std::uint64_t(cy)*0xC2B2AE3D27D4EB4Full);h^=h>>29;h*=0xBF58476D1CE4E5B9ull;h^=h>>32;return std::uint32_t(h)&(Slots-1);}
    bool usable(const Slot& s)const{return s.live&&frame_-s.seen<=Gap;}
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
            found->live=false;}
        const std::uint32_t at=slotOf(model,cell(root[0]),cell(root[1]));
        for(unsigned k=0;k<Probes;++k){Slot& s=slots_[(at+k)&(Slots-1)];
            if(usable(s))continue;
            s.model=model;for(unsigned c=0;c<3;++c)s.root[c]=root[c];s.seen=frame_;s.live=true;return;}
        if(diagnostics_)++overflows;
    }
public:
    // Diagnostics only (counted when begin(true) opened the pass; no cost otherwise): overflows since the start (a full 4-slot chain: the
    // caster was not remembered), bandHeld in the last pass (casters kept in the actor map by the hold while beyond Inset).
    unsigned long overflows=0;unsigned bandHeld=0;
    // Once per pass, before the casters.
    void begin(bool diagnostics=false){++frame_;diagnostics_=diagnostics;bandHeld=0;}
    void reset(){for(auto& s:slots_)s=Slot{};frame_=1;}
    // The pass did not run (actor map off, a reset): every remembered caster expires at once, O(1).
    void invalidate(){frame_+=Gap+1;}
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
    unsigned remembered()const{unsigned n=0;for(const auto& s:slots_)n+=usable(s)&&s.seen==frame_;return n;}
};
}
