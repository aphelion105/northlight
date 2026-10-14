#pragma once
#include <cstdint>

// Portable decision logic (no D3D/Windows) for releasing idle world-mesh pool capacity.
// The world mesh is double buffered: the active slot is drawn, the staging slot is reused by
// the next build. Pages are only ever grown, so after one huge mesh (a flight over a dense
// region, ~84 MiB) both slots stayed at that high-water size for the whole session (a player
// log held 159.5 MiB for 15 minutes of ~35 MiB meshes). Under DXVK every MANAGED buffer also
// keeps a host staging copy for its lifetime, so the idle capacity costs 32-bit address space.
//
// Rule, evaluated once per committed mesh (never per frame):
//   target  = 2 x the bytes the just-committed mesh occupies (active slot's used pages): the
//             active mesh plus a staging slot able to take a next build of the same size
//             without growing any page.
//   surplus = pool capacity (both slots, all pages) - target.
//   trim only after surplus > SurplusBytes on StreakCommits consecutive commits; any commit
//   at or below the threshold restarts the streak. A mesh size that oscillates between a
//   small and a large value therefore never trims (the large commit has no surplus), and a
//   stable size trims at most once; the trim leaves the pool near target, so no surplus
//   remains and nothing is re-created unless a larger mesh genuinely arrives.
// 16 MiB: about one typical 30-40 MiB mesh's allocation noise (page rounding, a few extra
// pages for a slightly larger tile set) and large enough that a trim frees something
// worthwhile. 3 commits: a streamed world commits in seconds, so a lone small mesh at a
// zone edge does not trigger it but a sustained smaller mesh does within a minute or so.
namespace NorthlightMeshPoolTrim {
constexpr uint64_t SurplusBytes=16ull<<20;
constexpr unsigned StreakCommits=3;
inline uint64_t target(uint64_t meshBytes){return 2*meshBytes;}
inline uint64_t surplus(uint64_t poolBytes,uint64_t meshBytes){const uint64_t t=target(meshBytes);return poolBytes>t?poolBytes-t:0;}
struct State {
    unsigned streak=0;
    // Returns true when the owner should trim now; the streak restarts after a trim.
    bool onCommit(uint64_t poolBytes,uint64_t meshBytes){
        if(surplus(poolBytes,meshBytes)<=SurplusBytes){streak=0;return false;}
        if(++streak<StreakCommits)return false;
        streak=0;return true;
    }
    void reset(){streak=0;}
};
}
