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

// Proactive tiers: profile reach -> MidReach (only when the profile exceeds it) -> BaseReach.
// The build picks the highest tier whose headroom the build's ENTRY sample meets, so a process
// that cannot carry the extended mesh never loads it just to be refused (a player log showed five
// 4096 <-> 928 reduce/restore cycles, each a full rebuild after a refused attempt).
// Derivation (measured, 32-bit client): mesh ~39 MiB per million triangles; reach 928 ~0.8-0.95M
// triangles (~35 MiB), 1856 ~1.3M (~+15 MiB), 4096 ~1.9-2.1M (~+43 MiB). A mesh lives twice in
// the double-buffered pool plus a transient plan/payload of similar size during the build, so
// the extra address space at peak is ~3x the per-mesh delta: ~+45 MiB (1856), ~+130-170 MiB (4096).
// Calibration from a Windows player log (601 build entries, largest free block ~419 MiB):
//   113 successful reach-4096 builds had entry available min 790, avg 950, max 1072 MiB;
//   5 refused reach-4096 builds (in-build sample fell to ~621-632 MiB under the 640 MiB floor) entered
//   with 798-879 MiB. The extended build drops the sample by ~250 MiB from its entry, so an entry of
//   900 MiB leaves ~650 MiB, above the 640 MiB in-build floor; every refusal entered below 880.
//   The refusals were about `available`, not contiguity (largest was ~419 MiB), so the block rule is only
//   the entry's 96 MiB plus slack for the 64 MiB terrain allocation: 128 MiB.
//   4096 class: 900 MiB available, 128 MiB contiguous
//   1856 class: 820 MiB available ( 96 MiB contiguous, the entry rule); its drop from entry is smaller
//   (~+15 MiB mesh delta against ~+43 MiB), and 820 is still above the 768 MiB plain entry.
// Any profile reach above MidReach is treated as the 4096 class (the profile parser caps it at
// 4096); a profile in (928, 1856] uses the 1856 requirement. 928 has no extra rule.
constexpr float MidReach=1856.f;
constexpr std::uint64_t MiB=1024ull*1024ull;
constexpr std::uint64_t ExtendedAvailable=900*MiB,ExtendedLargest=128*MiB;
constexpr std::uint64_t MidAvailable=820*MiB,MidLargest=96*MiB;
// Raising a lowered tier needs the requirement plus this margin (and BackoffMs since the last
// tier change or refusal), so a sample hovering around a threshold does not flip every build.
// Lowering is immediate: it only needs the plain requirement to fail.
constexpr std::uint64_t RaiseMargin=64*MiB;
// Does the entry sample carry `reach`? margin is added to the aggregate requirement only.
inline bool meets(float reach,std::uint64_t available,std::uint64_t largest,std::uint64_t margin=0){
    if(!extended(reach))return true;
    const bool mid=reach<=MidReach;
    return available>=(mid?MidAvailable:ExtendedAvailable)+margin&&largest>=(mid?MidLargest:ExtendedLargest);
}
struct State {
    bool refused=false;uint32_t refusedAt=0;std::uint64_t restoreAvailable=0,restoreLargest=0;
    // Lowered tier (0: none; reach follows the profile) and the tick of its last change.
    float tier=0;uint32_t tierSince=0;
    // DWORD tick wrap safe: unsigned subtraction.
    bool holding(uint32_t now)const{return refused&&uint32_t(now-refusedAt)<BackoffMs;}
};
struct Choice { float reach=BaseReach;bool reduced=false,restored=false,tierChanged=false;float tierFrom=0; };
// reach for the next build of a region whose profile reach is `profile`.
// entryAvailable/entryLargest: this build's stage-entry sample (unknown: no memory rule).
inline Choice choose(State& state,float profile,uint32_t now,std::uint64_t entryAvailable=~0ull,std::uint64_t entryLargest=~0ull){
    Choice c;c.reach=profile;
    const bool belowRestore=state.refused&&extended(profile)&&uint32_t(now-state.refusedAt)<RestoreHoldMaxMs&&
        (entryAvailable<state.restoreAvailable||entryLargest<state.restoreLargest);
    if(state.holding(now)||belowRestore){if(extended(profile)){c.reach=BaseReach;c.reduced=true;}return c;}
    const bool cleared=state.refused&&extended(profile);state.refused=false;
    // Unknown memory (~0 defaults): no memory rule, profile reach.
    if(extended(profile)&&entryAvailable!=~0ull){
        float best=BaseReach;
        const float cands[3]={profile,MidReach,BaseReach};
        for(float k:cands){if(k<=profile&&meets(k,entryAvailable,entryLargest)){best=k;break;}}
        const float cur=state.tier>0&&state.tier<profile?state.tier:profile; // this zone's current ceiling
        float eff=cur;
        if(best<cur)eff=best;                                                // drop at once
        else if(cur<profile&&uint32_t(now-state.tierSince)>=BackoffMs)
            for(float k:cands)if(k>cur&&k<=profile&&meets(k,entryAvailable,entryLargest,RaiseMargin)){eff=k;break;}
        if(eff!=cur){c.tierChanged=true;c.tierFrom=cur;state.tier=eff>=profile?0.f:eff;state.tierSince=now;}
        c.reach=eff;
    }
    c.restored=cleared&&extended(c.reach);return c;
}
// A stage of a build at `usedReach` was refused for MEMORY. True: rebuild at BaseReach now.
// Only an extended build reduces; non-memory errors and base-reach builds never call this.
// restoreAvailable/restoreLargest: entry sample the refused attempt would have needed (0: backoff only).
inline bool memoryRefused(State& state,float usedReach,uint32_t now,std::uint64_t restoreAvailable=0,std::uint64_t restoreLargest=0){
    if(!extended(usedReach))return false;
    state.tier=BaseReach;state.tierSince=now; // a later raise needs the hold and margin too
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
