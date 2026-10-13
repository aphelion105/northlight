#pragma once
#include <cstddef>
#include "replay_bounds_schedule.h"
#include <cstdint>
#include <vector>

// Visit order of the replay bounds passes (replay_bounds_job.h, pointCalculateReplayBounds): the passes walk the draw list from a
// rotating cursor under fixed time/call budgets (replay_envelopes.inl canReady/canHeavy/canBuild). When a crowd needs more than the
// budget, the packets after the point where it ran out stay without bounds this frame, and which ones those are rotates. Casters
// near the player must not be among them: a caster without bounds is never proven inside the actor map's inset and falls to the
// near cascade for that frame. NearList collects, in the loop that already pins every packet's inputs (no extra pass, capacity
// reused), the packets whose captured palette root lies within Yards of the shadow pivot; nearFirst visits exactly those first, then
// the normal rotating pass runs unchanged (it skips packets that already have bounds). Same budgets, same total work.
// The near walk is itself a NorthlightReplaySchedule::pass over the list, with one rotating start per phase kept in the List across
// frames: a packet that spends a whole phase moves the next frame's start past it, so a tail of expensive near packets cannot miss
// every frame. It may use at most half of a phase's operation (and vertex) budget (replay_bounds_job.h), so the rotating pass over all
// packets always has the other half: the far packets are not starved.
// Yards: the actor map holds 12 yd around the pivot in light-space x/y (actor_shadow_map.h Radius) and any depth within the cascade's
// range, so a caster in it can be farther from the pivot than 12 yd along the light; 24 (twice the radius) only orders the visits,
// every other packet is still visited by the rotating pass.
namespace NorthlightReplayBoundsNear {
inline constexpr float Yards=24.f;
struct List {
    float centre[3]={};float squared=0;bool on=false;std::vector<std::uint32_t> index;std::size_t cursor[3]={}; /* per phase, kept across frames (begin() leaves them) */
    void begin(const float* pivot,bool valid){index.clear();on=valid&&pivot;if(on)for(unsigned k=0;k<3;++k)centre[k]=pivot[k];squared=Yards*Yards;}
    void consider(std::size_t i,bool rooted,const float* root){
        if(!on||!rooted)return;
        const float x=root[0]-centre[0],y=root[1]-centre[1],z=root[2]-centre[2];
        if(x*x+y*y+z*z<=squared){try{index.push_back(std::uint32_t(i));}catch(...){on=false;}}
    }
};
// Visit the listed packets of a phase from its rotating start while canWork() (the phase can work and the near share is not spent), with
// pass()'s anti-pin rule and Budget handling; visit(index) returns true for a packet that ran out of budget. True: the walk ended
// because canWork() turned false, not because every listed packet was seen.
template<class CanWork,class Visit> bool nearFirst(List* list,unsigned phase,CanWork canWork,Visit visit){
    if(!list||phase>=3||list->index.empty())return false;
    const auto& index=list->index;
    bool stopped=false;
    list->cursor[phase]=NorthlightReplaySchedule::pass(index.size(),list->cursor[phase]%index.size(),
        [&]{const bool can=canWork();stopped=!can;return can;},[&](std::size_t k){return visit(std::size_t(index[k]));});
    return stopped;
}
}
