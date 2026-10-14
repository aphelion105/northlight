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
//   need    = the LARGEST mesh (bytes of the active slot's used pages) among the last WindowCommits
//             commits, including this one. A streamed world commits every ~3 s, so 32 commits cover
//             ~90-100 s: block-alternating sizes (4 x 40 MiB then 4 x 70 MiB) keep the large one in the
//             window and never look like surplus. Sizing by the CURRENT mesh trimmed ~24 times per 400
//             commits in such a pattern, each trim re-created by the next large mesh (GiBs of churn).
//   target  = 2 x need: the active mesh plus a staging slot able to take the next build without growth.
//   surplus = pool capacity (both slots, all pages) - target.
//   armed   = surplus > SurplusBytes on StreakCommits consecutive commits. Once armed it stays armed
//             while the surplus stays above the threshold, so a trim spread over several commits does
//             not rebuild the streak between its steps; any commit at or below the threshold disarms it.
//   Each armed commit asks the owner to release at most MaxReleaseBytes (highest pages first), so one
//   frame never frees ~90 MiB (~180 Release calls). The remaining surplus is released on the commits
//   that follow; the target never undershoots a recent mesh, so nothing released is needed again
//   within the window.
// 16 MiB: about one typical 30-40 MiB mesh's allocation noise (page rounding, a few extra pages for a
// slightly larger tile set). 3 commits: a lone small mesh at a zone edge does not arm it. 32 MiB per
// commit: ~64 Release calls at the 0.5 MiB page granularity seen in logs, a bounded render-thread cost.
namespace NorthlightMeshPoolTrim {
constexpr uint64_t SurplusBytes=16ull<<20;
constexpr uint64_t MaxReleaseBytes=32ull<<20;
constexpr unsigned StreakCommits=3;
constexpr unsigned WindowCommits=32;
inline uint64_t target(uint64_t meshBytes){return 2*meshBytes;}
inline uint64_t surplus(uint64_t poolBytes,uint64_t meshBytes){const uint64_t t=target(meshBytes);return poolBytes>t?poolBytes-t:0;}
struct State {
    unsigned streak=0;
    // Fixed ring of the last WindowCommits mesh sizes (no allocation).
    uint64_t recent[WindowCommits]={};unsigned head=0,count=0;
    uint64_t windowMax()const{uint64_t m=0;for(unsigned i=0;i<count;++i)if(recent[i]>m)m=recent[i];return m;}
    // Records the commit and returns the bytes the owner should release now (0: nothing); at most
    // MaxReleaseBytes. The owner reports the pool size again on the next commit.
    uint64_t onCommit(uint64_t poolBytes,uint64_t meshBytes){
        recent[head]=meshBytes;head=(head+1)%WindowCommits;if(count<WindowCommits)++count;
        const uint64_t extra=surplus(poolBytes,windowMax());
        if(extra<=SurplusBytes){streak=0;return 0;}
        if(streak<StreakCommits&&++streak<StreakCommits)return 0;
        return extra<MaxReleaseBytes?extra:MaxReleaseBytes;
    }
    void reset(){streak=0;head=0;count=0;for(auto& r:recent)r=0;}
};
}
