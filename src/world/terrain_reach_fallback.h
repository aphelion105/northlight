#pragma once
#include <cstdint>

// Portable decision logic (no D3D/Windows; the caller passes the tick clock).
// A 32-bit process whose largest free block is ~63 MiB cannot build the 64 MiB
// terrain allocation of an extended (4096) shadow reach, and the build was retried
// every second until the camera left the region. A memory refusal of an EXTENDED
// build now rebuilds at the base reach at once; no admission margin is relaxed.
namespace NorthlightTerrainReach {
constexpr float BaseReach=928.f; // == NorthlightShadowTerrain::Radius
// Restore rule: the extended reach is tried again no sooner than BackoffMs after the
// LAST memory refusal of an extended build (each refused retry restarts the clock), and
// only when the build's entry sample reaches restoreAvailable: the refused attempt's entry
// sample plus its shortfall. A player log restored every 30 s at an unchanged ~630 MiB
// refusal, so every build first loaded the extended terrain and then threw it away.
constexpr uint32_t BackoffMs=30000;
// The memory rule holds at most this long after the last refusal; the owner also resets
// the state on a map change, so one heavy zone never pins the base reach for the session.
constexpr uint32_t RestoreHoldMaxMs=300000;
inline bool extended(float reach){return reach>BaseReach;}
struct State {
    bool refused=false;uint32_t refusedAt=0;std::uint64_t restoreAvailable=0,restoreLargest=0;
    // DWORD tick wrap safe: unsigned subtraction.
    bool holding(uint32_t now)const{return refused&&uint32_t(now-refusedAt)<BackoffMs;}
};
struct Choice { float reach=BaseReach;bool reduced=false,restored=false; };
// reach for the next build of a region whose profile reach is `profile`.
// entryAvailable/entryLargest: this build's stage-entry sample (unknown: no memory rule).
inline Choice choose(State& state,float profile,uint32_t now,std::uint64_t entryAvailable=~0ull,std::uint64_t entryLargest=~0ull){
    Choice c;c.reach=profile;
    const bool belowRestore=state.refused&&extended(profile)&&uint32_t(now-state.refusedAt)<RestoreHoldMaxMs&&
        (entryAvailable<state.restoreAvailable||entryLargest<state.restoreLargest);
    if(state.holding(now)||belowRestore){if(extended(profile)){c.reach=BaseReach;c.reduced=true;}return c;}
    c.restored=state.refused&&extended(profile);state.refused=false;return c;
}
// A stage of a build at `usedReach` was refused for MEMORY. True: rebuild at BaseReach now.
// Only an extended build reduces; non-memory errors and base-reach builds never call this.
// restoreAvailable/restoreLargest: entry sample the refused attempt would have needed (0: backoff only).
inline bool memoryRefused(State& state,float usedReach,uint32_t now,std::uint64_t restoreAvailable=0,std::uint64_t restoreLargest=0){
    if(!extended(usedReach))return false;
    state.refused=true;state.refusedAt=now;state.restoreAvailable=restoreAvailable;state.restoreLargest=restoreLargest;return true;
}
// One geometry-memory stall episode (log begin/end, never per retry).
struct Episode {
    bool active=false,reduced=false;uint32_t since=0;unsigned attempts=0;
    bool refused(uint32_t now){const bool begin=!active;if(begin){active=true;reduced=false;since=now;attempts=0;}++attempts;return begin;}
    void markReduced(){reduced=true;}
    bool end(uint32_t now,uint32_t& ms,unsigned& tries){
        if(!active)return false;active=false;ms=uint32_t(now-since);tries=attempts;return true;
    }
};
} // namespace NorthlightTerrainReach
