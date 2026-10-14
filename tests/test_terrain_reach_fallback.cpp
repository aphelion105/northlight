#include "terrain_reach_fallback.h"
#include "geometry_memory.h"
#include <cassert>
#include <iostream>
using namespace NorthlightTerrainReach;
namespace GM=NorthlightGeometryMemory;
int main(){
    constexpr float Ext=4096;
    // Observed Bael Modan sample (aggregate 760 MiB, largest block 63 MiB), synthetic growth checks.
    const GM::Sample observed{760*GM::MiB,63*GM::MiB,true},unfragmented{800*GM::MiB,63*GM::MiB,true};
    for(double mib:{1.0,23.58,37.92,64.0}){
        const uint64_t bytes=uint64_t(mib*GM::MiB);
        assert(!GM::admits(observed,GM::buildBudget(bytes)));   // full-reach sequence refused
        if(mib>31)assert(!GM::admits(unfragmented,GM::buildBudget(bytes))); // needs bytes+32 MiB contiguous
    }
    // Reduced reach: allocations stay well below the 96 MiB block requirement of the 64 MiB one.
    for(double mib:{1.0,8.0,16.0,24.0})assert(GM::admits(unfragmented,GM::buildBudget(uint64_t(mib*GM::MiB))));
    // Aggregate margin still applies to the reduced build (no margin was relaxed).
    assert(!GM::admits(observed,GM::buildBudget(24*GM::MiB)));
    assert(GM::buildBudget().available==768*GM::MiB&&GM::ContiguousMargin==32*GM::MiB&&GM::ProcessReserve==512*GM::MiB);

    // The sample is taken fresh per call and drops by what the stage itself holds: the extended attempt has
    // 64 MiB-class buffers live when it asks for the next one; freeing them (the retry) restores the sample.
    {uint64_t held=0;const uint64_t entryAvail=980*GM::MiB,entryLargest=754*GM::MiB;
     auto sample=[&]{return GM::Sample{entryAvail-held,held?63*GM::MiB:entryLargest,true};};
     auto admit=[&](uint64_t bytes){return GM::admits(sample(),GM::buildBudget(bytes));};
     held=220*GM::MiB;                              // extended terrain scene + merged vertices already built (mid-build)
     assert(sample().available<768*GM::MiB&&sample().largest==63*GM::MiB);
     assert(!admit(64*GM::MiB));                    // extended refused
     held=0;                                        // torn down before the reduced attempt
     assert(admit(1*GM::MiB)&&admit(24*GM::MiB)); // reduced allocations admitted again, same margins
    }
    // Fallback decisions.
    {State s;auto c=choose(s,Ext,1000);assert(c.reach==Ext&&!c.reduced&&!c.restored);
     assert(memoryRefused(s,c.reach,1000));                         // reduce on first refusal
     c=choose(s,Ext,1001);assert(c.reach==BaseReach&&c.reduced&&!c.restored);
     assert(!memoryRefused(s,c.reach,1001));                        // reduced build: normal retry path
     c=choose(s,Ext,1000+BackoffMs-1);assert(c.reduced&&c.reach==BaseReach); // backoff respected
     c=choose(s,Ext,1000+BackoffMs);assert(c.reach==Ext&&!c.reduced&&c.restored); // restored after it
     c=choose(s,Ext,1000+BackoffMs+1);assert(c.reach==Ext&&!c.restored);        // restored only once
     assert(memoryRefused(s,Ext,50000));                            // refused again: clock restarts
     assert(choose(s,Ext,50000+BackoffMs-1).reduced&&!choose(s,Ext,50000+BackoffMs).reduced);}
    {State s;assert(!memoryRefused(s,BaseReach,5));assert(!s.refused);       // not extended: no reduce
     auto c=choose(s,BaseReach,6);assert(c.reach==BaseReach&&!c.reduced&&!c.restored);
     assert(!memoryRefused(s,0,5));}
    {State s;auto c=choose(s,Ext,10);assert(c.reach==Ext&&!s.refused);}       // non-memory error never touches state
    {State s;memoryRefused(s,Ext,1);auto c=choose(s,BaseReach,2);assert(c.reach==BaseReach&&!c.reduced&&!c.restored); // other zone
     assert(!s.holding(2)==false);c=choose(s,BaseReach,1+BackoffMs);assert(!s.refused);
     c=choose(s,Ext,1+BackoffMs+1);assert(c.reach==Ext&&!c.restored);}
    {State s;const uint32_t t=0xFFFFFFF0u;assert(memoryRefused(s,Ext,t));    // DWORD tick wrap
     assert(choose(s,Ext,t+5).reduced);assert(choose(s,Ext,t+BackoffMs-1).reduced);
     auto c=choose(s,Ext,t+BackoffMs);assert(c.reach==Ext&&c.restored);}
    {State s;const uint32_t t=0xFFFFFFF0u;memoryRefused(s,Ext,t);
     assert(choose(s,Ext,uint32_t(t+0x20)).reduced);}                         // now wrapped past zero, still held
    // Episode.
    {Episode e;uint32_t ms=0;unsigned n=0;assert(!e.end(5,ms,n));
     assert(e.refused(100));assert(!e.refused(1100));assert(!e.refused(2100));e.markReduced();
     assert(e.end(0xFFFFFFFFu+1u+400u,ms,n)==true&&ms==uint32_t(400u-100u)&&n==3&&e.reduced);
     assert(!e.end(1,ms,n));assert(e.refused(9)&&!e.reduced);}
    // Episode bookkeeping: begin once, end once; a new episode begins after an end.
    {Episode e;int begins=0,ends=0;uint32_t ms=0;unsigned n=0;
     for(int i=0;i<3;++i){begins+=e.refused(i*10);}ends+=e.end(40,ms,n);ends+=e.end(50,ms,n);
     begins+=e.refused(60);ends+=e.end(70,ms,n);assert(begins==2&&ends==2);}
    // Memory restore rule (0.3.212 player log: every 30 s restore refused again at ~630 MiB, entry ~800).
    // (+600 MiB on every sample so the proactive tier requirement, 1100 MiB + margin, is met as well.)
    {State s;const uint64_t entry=1400*GM::MiB,need=GM::inBuildBudget(1*GM::MiB).available,seen=630*GM::MiB;
     assert(memoryRefused(s,Ext,1000,entry+(need-seen)));assert(s.restoreAvailable==1410*GM::MiB);
     assert(choose(s,Ext,1000+BackoffMs,entry).reduced);                 // backoff over, entry unchanged: still base reach
     assert(choose(s,Ext,1000+5*BackoffMs,1409*GM::MiB).reduced&&s.refused);
     auto c=choose(s,Ext,1000+5*BackoffMs,1410*GM::MiB);assert(c.reach==Ext&&c.restored&&!s.refused); // enough at entry: restored once
     assert(memoryRefused(s,Ext,9000,0));assert(!choose(s,Ext,9000+BackoffMs,0).reduced);}    // 0 = backoff only (old rule)
    {State s;memoryRefused(s,Ext,1,UINT64_MAX);                                  // overflowed estimate: never restored by memory
     assert(choose(s,Ext,1+BackoffMs,UINT64_MAX-1).reduced);
     auto c=choose(s,BaseReach,2+BackoffMs,0);assert(c.reach==BaseReach&&!c.reduced&&!s.refused);} // a base-reach zone still clears it
    // Entry lift (0.3.212 player log: plan-deferred 20, 162 and 482 attempts, the BVH rebuilt every second).
    {GM::EntryLift lift;const auto plain=GM::buildBudget(64*GM::MiB);
     assert(!lift.active(0)&&lift.apply(plain,0).available==768*GM::MiB);
     // Entry at 770, the in-build check refused at 630 (needs 640): the next entry needs 780.
     lift.refused(770*GM::MiB,GM::inBuildBudget().available,630*GM::MiB,100);
     assert(lift.available==780*GM::MiB&&lift.apply(plain,100).available==780*GM::MiB);
     assert(lift.apply(plain,100).largest==plain.largest);                      // contiguous rule untouched
     assert(!GM::admits({775*GM::MiB,500*GM::MiB,true},lift.apply(plain,200)));  // no retry at the same level
     assert(GM::admits({780*GM::MiB,500*GM::MiB,true},lift.apply(plain,200)));
     assert(lift.apply(plain,100+GM::LiftMs-1).available==780*GM::MiB);
     assert(lift.apply(plain,100+GM::LiftMs).available==768*GM::MiB&&!lift.active(100+GM::LiftMs)); // expires: no lockout
     // Never below the plain floor, capped above it, overflow-safe, cleared on publish.
     lift.refused(700*GM::MiB,640*GM::MiB,700*GM::MiB,0);assert(lift.apply(plain,1).available==768*GM::MiB);
     lift.refused(3000*GM::MiB,640*GM::MiB,0,0);assert(lift.available==768*GM::MiB+GM::LiftCap);
     lift.refused(UINT64_MAX,UINT64_MAX,0,0);assert(lift.available==768*GM::MiB+GM::LiftCap);
     lift.clear();assert(lift.apply(plain,1).available==768*GM::MiB);
     const uint32_t t=0xFFFFFF00u;lift.refused(800*GM::MiB,640*GM::MiB,600*GM::MiB,t); // tick wrap
     assert(lift.apply(plain,uint32_t(t+0x200)).available==840*GM::MiB&&!lift.active(uint32_t(t+GM::LiftMs)));
     assert(!GM::admits({0,0,false},lift.apply(GM::Budget{},0)));}
    // A refusal for want of one free block (no aggregate shortfall) must not retry at once either.
    {GM::EntryLift lift;const auto plain=GM::buildBudget(64*GM::MiB);const auto big=GM::inBuildBudget(64*GM::MiB); // 96 MiB contiguous
     lift.refused(900*GM::MiB,big.available,890*GM::MiB,0,big.largest+32*GM::MiB);
     assert(lift.apply(plain,1).available==900*GM::MiB&&lift.apply(plain,1).largest==128*GM::MiB);
     assert(!GM::admits({950*GM::MiB,96*GM::MiB,true},lift.apply(plain,1))&&GM::admits({950*GM::MiB,128*GM::MiB,true},lift.apply(plain,1)));
     lift.refused(900*GM::MiB,640*GM::MiB,700*GM::MiB,0,32*GM::MiB);assert(lift.apply(plain,1).largest==plain.largest);} // never below the plain block
    {State s;memoryRefused(s,Ext,0,700*GM::MiB,96*GM::MiB);
     assert(choose(s,Ext,BackoffMs,1400*GM::MiB,95*GM::MiB).reduced);                // aggregate fine, block too small
     assert(choose(s,Ext,BackoffMs,1400*GM::MiB,192*GM::MiB).restored);}
    // The memory rule holds at most RestoreHoldMaxMs after the last refusal (no session-long base reach).
    {State s;memoryRefused(s,Ext,0,UINT64_MAX,UINT64_MAX);
     assert(choose(s,Ext,RestoreHoldMaxMs-1,0,0).reduced);
     auto c=choose(s,Ext,RestoreHoldMaxMs,3000*GM::MiB,512*GM::MiB);assert(c.reach==Ext&&c.restored);}
    // Proactive tiers (4096 profile): the highest tier the entry sample carries, never above the profile.
    {State s;uint32_t t=1000;
     auto c=choose(s,Ext,t,1200*GM::MiB,256*GM::MiB);assert(c.reach==Ext&&!c.tierChanged&&s.tier==0);       // rich: profile
     c=choose(s,Ext,t,1099*GM::MiB,256*GM::MiB);assert(c.reach==MidReach&&c.tierChanged&&c.tierFrom==Ext&&!c.reduced); // 4096 short of available
     c=choose(s,Ext,t,1200*GM::MiB,191*GM::MiB);assert(c.reach==MidReach&&!c.tierChanged);                 // lowered tier is kept (hold)
     c=choose(s,Ext,t,949*GM::MiB,256*GM::MiB);assert(c.reach==BaseReach&&c.tierChanged&&c.tierFrom==MidReach); // drops again at once
     c=choose(s,Ext,t,767*GM::MiB,256*GM::MiB);assert(c.reach==BaseReach&&!c.tierChanged);                 // already lowest: no log
     assert(!c.reduced&&!s.refused);}                                                                      // tiers are not refusals
    {State s;choose(s,Ext,0,900*GM::MiB,256*GM::MiB);assert(s.tier==BaseReach);                          // 900 < 950: base
     // 1856 needs 950 (+128 to raise) after the hold, in one step; and 4096 needs 1100+128 and 192 MiB contiguous.
     assert(choose(s,Ext,BackoffMs-1,5000*GM::MiB,512*GM::MiB).reach==BaseReach);                          // hold time not over
     auto c=choose(s,Ext,BackoffMs,1077*GM::MiB,512*GM::MiB);assert(c.reach==BaseReach&&!c.tierChanged);   // 1856 margin: 950+128=1078
     c=choose(s,Ext,BackoffMs,1078*GM::MiB,512*GM::MiB);assert(c.reach==MidReach&&c.tierChanged&&c.tierFrom==BaseReach);
     assert(choose(s,Ext,BackoffMs+1,1227*GM::MiB,512*GM::MiB).reach==MidReach);                           // 4096 margin: 1228
     assert(choose(s,Ext,2*BackoffMs-1,5000*GM::MiB,512*GM::MiB).reach==MidReach);                         // hold restarts at each change
     assert(choose(s,Ext,2*BackoffMs,1228*GM::MiB,191*GM::MiB).reach==MidReach);                           // contiguous short
     c=choose(s,Ext,2*BackoffMs,1228*GM::MiB,192*GM::MiB);assert(c.reach==Ext&&c.tierChanged&&c.tierFrom==MidReach&&s.tier==0);}
    {State s;const uint32_t t=0xFFFFFF00u;choose(s,Ext,t,900*GM::MiB);                                     // tick wrap of the hold
     assert(choose(s,Ext,t+BackoffMs-1,4000*GM::MiB).reach==BaseReach&&choose(s,Ext,t+BackoffMs,4000*GM::MiB).reach==Ext);}
    // No flip-flop: a sample oscillating around the threshold changes tier at most once per hold.
    {State s;int changes=0;uint32_t t=0;
     for(int i=0;i<200;++i,t+=1000){const uint64_t a=(i%2?1150:1050)*GM::MiB;changes+=choose(s,Ext,t,a,256*GM::MiB).tierChanged;}
     assert(changes==1);}                                                                                  // dropped once, never raised (needs 1228)
    // Profile 1856 and 928 zones, and mixed zones sharing the state.
    {State s;auto c=choose(s,MidReach,0,949*GM::MiB,256*GM::MiB);assert(c.reach==BaseReach&&c.tierChanged&&c.tierFrom==MidReach);
     c=choose(s,MidReach,1,5000*GM::MiB,0);assert(c.reach==BaseReach);                                       // hold applies
     c=choose(s,MidReach,BackoffMs,1078*GM::MiB,96*GM::MiB);assert(c.reach==MidReach&&s.tier==0);
     assert(choose(s,MidReach,BackoffMs+1,950*GM::MiB,96*GM::MiB).reach==MidReach);}                      // 1856 never asks for the 4096 rule
    {State s;auto c=choose(s,BaseReach,0,1*GM::MiB,0);assert(c.reach==BaseReach&&!c.tierChanged&&s.tier==0);}
    {State s;choose(s,Ext,0,900*GM::MiB,256*GM::MiB);                                                       // tier 928 from a 4096 zone
     auto c=choose(s,MidReach,1,5000*GM::MiB,512*GM::MiB);assert(c.reach==BaseReach&&!c.tierChanged);       // 1856 zone shares the lowered tier: hold applies
     assert(choose(s,BaseReach,2,0,0).reach==BaseReach);}
    // Unknown memory: no memory rule, profile reach, state untouched.
    {State s;auto c=choose(s,Ext,0);assert(c.reach==Ext&&!c.tierChanged&&s.tier==0);
     c=choose(s,Ext,1,~0ull,~0ull);assert(c.reach==Ext);
     choose(s,Ext,2,1000*GM::MiB,256*GM::MiB);c=choose(s,Ext,3);assert(c.reach==Ext&&s.tier==MidReach);}
    // Refusal fallback still wins over the tier and sets the base tier (a raise then needs hold + margin).
    {State s;assert(choose(s,Ext,0,3000*GM::MiB,512*GM::MiB).reach==Ext);
     assert(memoryRefused(s,Ext,10,0,0)&&s.tier==BaseReach&&s.tierSince==10);
     auto c=choose(s,Ext,11,3000*GM::MiB,512*GM::MiB);assert(c.reach==BaseReach&&c.reduced&&!c.tierChanged);
     c=choose(s,Ext,10+BackoffMs,1227*GM::MiB,512*GM::MiB);assert(c.reach==MidReach&&!c.reduced&&c.restored); // refusal cleared; the tier margin caps the raise at 1856
     c=choose(s,Ext,10+2*BackoffMs,1228*GM::MiB,512*GM::MiB);assert(c.reach==Ext&&!c.restored&&s.tier==0);
     assert(memoryRefused(s,MidReach,20+3*BackoffMs));}                                                     // a refused 1856 build also falls to 928
    // The after-BVH gate keeps the in-build floor: the logged 720 MiB plan refusal now proceeds.
    assert(GM::admits({720*GM::MiB,502*GM::MiB,true},GM::inBuildBudget())&&!GM::admits({720*GM::MiB,502*GM::MiB,true},GM::buildBudget()));
    std::cout<<"terrain reach fallback PASS\n";
}
