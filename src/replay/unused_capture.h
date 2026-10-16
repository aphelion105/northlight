#pragma once
// Unused-capture memory (task 43, item B). A skinned draw captured as a shadow replay that no consumer used (no cascade drew it, no point cube,
// no GI packet, not kept by the static proof) for MinStreak consecutive information frames, at the same identity and the same 1 yd root cell,
// is predicted unused again. Mode 1 only counts the prediction (the draw is still captured, the verdict is checked against reality);
// Mode 2 skips its capture copy, except every RefreshEvery-th time, which re-verifies it (a caster that moves leaves its root cell and is
// captured at once; one the light frustum turns toward is missed for at most RefreshEvery-1 captures of that draw). Portable: no D3D, no game memory.
#include <cmath>
#include <cstdint>
#include <unordered_map>
namespace NorthlightUnusedCapture {
constexpr unsigned Mode=0; /* 0 off, 1 predict and verify only (nothing is skipped), 2 skip the predicted capture. Off: a live-realm crowd showed
                               0 misses but under 1 % of the unused captures predicted, for a hash and a lookup per captured draw (issue #63) */
constexpr unsigned MinStreak=8,RefreshEvery=8,Stale=4; /* Stale: capture frames an entry may go unseen before it is forgotten */
constexpr std::size_t MaxEntries=8192;
constexpr unsigned SweepEvery=8;
inline std::uint64_t key(std::uintptr_t shader,unsigned count,unsigned vertexTotal,const float* root){
    std::uint64_t h=14695981039346656037ull;auto mix=[&](std::uint64_t n){h=(h^n)*1099511628211ull;};
    mix(shader);mix(count);mix(vertexTotal);
    for(unsigned k=0;k<3;++k)mix(std::uint64_t(std::uint32_t(std::int32_t(std::floor(root[k])))));
    return h;}
class Memory {
    struct Entry {unsigned streak=0,skips=0,seen=0;};
    std::unordered_map<std::uint64_t,Entry> map_;unsigned frame_=1;
public:
    unsigned frame()const{return frame_;} /* the stamp a consumer writes into Replay::usedStamp this frame */
    std::size_t size()const{return map_.size();}
    void clear(){map_.clear();frame_=1;}
    // True: this draw is predicted unused (and, in Mode 2, not due for its refresh capture).
    bool predict(std::uint64_t k){
        auto it=map_.find(k);if(it==map_.end())return false;auto& e=it->second;
        if(frame_-e.seen>Stale){map_.erase(it);return false;}
        if(e.streak<MinStreak)return false;
        e.seen=frame_;
        if(++e.skips%RefreshEvery==0)return false; /* due: captured this time, its verdict observed */
        return true;}
    // End of a capture frame, per captured draw. info: every consumer that could use a draw ran this frame (otherwise unused proves nothing).
    void observe(std::uint64_t k,bool used,bool info){
        auto it=map_.find(k);
        if(used){if(it!=map_.end())map_.erase(it);return;}
        if(!info)return;
        if(it==map_.end()){if(map_.size()>=MaxEntries)return;it=map_.emplace(k,Entry{}).first;}
        auto& e=it->second;if(e.streak<1000)++e.streak;e.seen=frame_;}
    // A full sweep every SweepEvery frames drops what went unseen for more than Stale frames (walkers leave a key behind per 1 yd step).
    void endFrame(){++frame_;
        if(frame_%SweepEvery==0)for(auto it=map_.begin();it!=map_.end();){if(frame_-it->second.seen>Stale)it=map_.erase(it);else ++it;}}
};
}
