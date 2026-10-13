#pragma once
#include <cstdint>

// Low address-space guard for the 32-bit (Large Address Aware) client. The
// async sampler thread measures; the render thread only compares numbers and,
// at the frame boundary, drops mod caches that are rebuilt on demand. The mod
// owns a few hundred MiB of a 4 GiB space: this delays, it cannot prevent,
// exhaustion caused by the game or driver. Admission checks stay synchronous.
namespace NorthlightMemoryGuard {
constexpr std::uint64_t MiB=1024ull*1024ull;
struct Policy {
    // Pressure: largest free region or aggregate free VA below these.
    std::uint64_t enterLargest=160*MiB,enterAvailable=400*MiB;
    // Recovery: both above these for recoverHoldMs of consecutive samples.
    std::uint64_t recoverLargest=256*MiB,recoverAvailable=640*MiB;
    std::uint32_t recoverHoldMs=10000,trimCooldownMs=20000;
    // Address-space walk cadence (sampler thread, ~6-50 ms each on Windows).
    std::uint32_t sampleMs=3000,pressureSampleMs=1000,reportMs=10000;
    // Demand pressure: the geometry builder stalls for memory below its own floor
    // (768 MiB at entry), far above enterAvailable. Recovery from a pressure episode
    // that a demand entered needs the largest demand seen plus this, so the restored
    // full caps cannot regrow straight back into the next region's stall.
    std::uint64_t demandRegrowth=128*MiB;
    // A demand floor with no demand for this long is dropped: recovery returns to the
    // policy level instead of holding halved caps for the rest of the session.
    std::uint32_t demandDecayMs=300000;
};
struct Decision {
    bool trim=false,entered=false,recovered=false;
    bool afterTrim=false; // first sample walked after the last completed trim
    bool demand=false;    // this entry/trim was caused by the builder's demand, not the guard's own thresholds
    bool report=false;    // periodic diagnostic line due (caller gates on Diagnostics)
};
// Ticks are GetTickCount() milliseconds; every comparison is wrap-safe. Feed
// only exact full address-space walks, never admission-witness lower bounds.
class Guard {
    Policy policy_;
    bool pressure_=false,trimmed_=false,healthy_=false,requested_=false,reported_=false,afterPending_=false;
    bool demandOnly_=false; // pressure entered by a demand and no own-threshold low sample since
    std::uint32_t lastTrim_=0,trimDone_=0,healthySince_=0,lastRequest_=0,lastReport_=0,lastDemand_=0;
    std::uint64_t minAvailable_=~0ull,minLargest_=~0ull,demandFloor_=0;
    unsigned trims_=0,entries_=0,samples_=0;
    static bool after(std::uint32_t tick,std::uint32_t reference){return std::int32_t(tick-reference)>0;}
public:
    explicit Guard(Policy policy=Policy{}):policy_(policy){}
    const Policy& policy()const{return policy_;}
    // True when a new address-space walk should be requested now. The 1 s cadence is for the
    // guard's own low thresholds; a demand-only episode keeps the normal 3 s walk.
    bool requestDue(std::uint32_t now){
        if(requested_&&now-lastRequest_<(pressure_&&!demandOnly_?policy_.pressureSampleMs:policy_.sampleMs))return false;
        requested_=true;lastRequest_=now;return true;
    }
    // One valid sample; sampleTick is when the walk finished. demand: aggregate available
    // the geometry builder is waiting for (0: not stalled for memory).
    Decision update(std::uint64_t available,std::uint64_t largest,std::uint32_t sampleTick,std::uint64_t demand=0){
        Decision d;++samples_;
        if(available<minAvailable_)minAvailable_=available;
        if(largest<minLargest_)minLargest_=largest;
        if(afterPending_&&after(sampleTick,trimDone_)){afterPending_=false;d.afterTrim=true;}
        const bool low=largest<policy_.enterLargest||available<policy_.enterAvailable;
        const bool demanded=available<demand;
        if(demand)lastDemand_=sampleTick;
        else if(demandFloor_&&sampleTick-lastDemand_>=policy_.demandDecayMs)demandFloor_=0;
        if(demanded&&demand>demandFloor_)demandFloor_=demand;
        const bool healthy=largest>=policy_.recoverLargest&&available>=recoverAvailable();
        if(low){
            healthy_=false;demandOnly_=false;
            if(!pressure_){pressure_=true;++entries_;d.entered=true;}
            // Repeated trims while still low, at most once per cooldown.
            if(!trimmed_||sampleTick-lastTrim_>=policy_.trimCooldownMs){trimmed_=true;lastTrim_=sampleTick;++trims_;d.trim=true;}
        }else if(demanded&&!pressure_){
            // Halved caps for the episode and one trim (cooldown-limited); a demand alone
            // never repeats the trim while it stays unmet.
            healthy_=false;pressure_=true;demandOnly_=true;++entries_;d.entered=true;d.demand=true;
            if(!trimmed_||sampleTick-lastTrim_>=policy_.trimCooldownMs){trimmed_=true;lastTrim_=sampleTick;++trims_;d.trim=true;}
        }else if(pressure_&&healthy){
            if(!healthy_){healthy_=true;healthySince_=sampleTick;}
            if(sampleTick-healthySince_>=policy_.recoverHoldMs){pressure_=false;healthy_=false;demandOnly_=false;demandFloor_=0;d.recovered=true;}
        }else healthy_=false; // between the thresholds: keep the current state
        if(!reported_||sampleTick-lastReport_>=policy_.reportMs||d.trim||d.entered||d.recovered){reported_=true;lastReport_=sampleTick;d.report=true;}
        return d;
    }
    // The caller finished the trim a decision asked for (frame tick).
    void trimCompleted(std::uint32_t now){trimDone_=now;afterPending_=true;}
    bool pressure()const{return pressure_;}
    std::uint64_t demandFloor()const{return demandFloor_;}
    // Aggregate level recovery needs: the policy's, or the episode's largest demand plus regrowth.
    std::uint64_t recoverAvailable()const{
        const std::uint64_t floor=demandFloor_?demandFloor_+policy_.demandRegrowth:0;
        return floor>policy_.recoverAvailable?floor:policy_.recoverAvailable;
    }
    unsigned trims()const{return trims_;}
    unsigned entries()const{return entries_;}
    unsigned samples()const{return samples_;}
    std::uint64_t minAvailable()const{return samples_?minAvailable_:0;}
    std::uint64_t minLargest()const{return samples_?minLargest_:0;}
};
}
