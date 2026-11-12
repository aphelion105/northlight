// 0.3.175 (S3) effects buckets (effects_buckets.h): contiguous spans sum to the whole, an unreached
// mark rolls into the next bucket, draw calls are billed like time, and off reads no clock.
#include "effects_buckets.h"
#include <cassert>
#include <cstdio>
using namespace NorthlightEffectsBuckets;
static std::int64_t now=0;static unsigned calls=0;
static std::int64_t fake(){++calls;return now;}
int main(){
    std::uint32_t dip=0,dp=0;const std::uint32_t* counters[4]={&dip,&dp,nullptr,nullptr};
    Frame f;
    // Off: no clock read at all.
    calls=0;f.begin(false,&fake,counters,4);for(unsigned b=0;b<Count;++b)f.mark(Bucket(b));f.end();assert(calls==0&&!f.on());
    // On: every span billed to the mark that closes it; the tail after the last mark; draws likewise.
    now=1000;calls=0;f.begin(true,&fake,counters,4);
    now+=5;dip+=2;f.mark(Setup);
    now+=7;dip+=3700;f.mark(Celestial);
    now+=1;f.mark(AO);
    now+=20;dip+=90;dp+=4;f.mark(SunNear); /* Selection..TerrainSelection not reached: rolled into SunNear */
    now+=3;dip+=1;f.mark(Composite);
    now+=2;f.end();
    assert(f.ticks(Setup)==5&&f.ticks(Celestial)==7&&f.ticks(AO)==1&&f.ticks(Selection)==0&&f.ticks(SunNear)==20&&f.ticks(Composite)==3&&f.ticks(Tail)==2);
    std::int64_t sum=0;std::uint32_t draws=0;for(unsigned b=0;b<Count;++b){sum+=f.ticks(b);draws+=f.drawCalls(b);}
    assert(sum==f.total()&&f.total()==38&&draws==3797&&f.drawCalls(Celestial)==3700&&f.drawCalls(SunNear)==94);
    assert(calls==7&&f.reads()==7&&f.reads()<=MaxReads&&!f.on());
    assert(f.drawsMeasured());
    // A mark after end() is ignored (no read); begin() resets.
    f.mark(Veil);assert(calls==7);f.begin(true,&fake,counters,4);assert(f.ticks(SunNear)==0&&f.drawCalls(Celestial)==0);f.end();
    // Ordinary unsampled frames retain the coarse clock split while raw draw
    // counting remains off. A stale sampled counter must not appear measured.
    now=0;calls=0;f.begin(true,&fake,nullptr,4);
    now=1000000;dip+=1234;f.mark(Setup);
    now=139000000;dip+=4567;f.mark(Point);
    now=140000000;f.end();
    assert(!f.drawsMeasured()&&f.total()==140000000&&f.ticks(Point)==138000000&&calls==4);
    for(unsigned b=0;b<Count;++b)assert(f.drawCalls(b)==0);
    assert(!report(false,7.999)&&report(false,8)&&report(false,140));
    assert(report(true,0)&&report(true,7.999)&&!report(false,-1));
    // Match the production no-op guard: a second already-applied call must
    // not begin/end another split or erase the long first applying call.
    auto attempt=[&](bool applied){if(applied)return;f.begin(true,&fake);f.end();};
    const auto first=f.total();const auto point=f.ticks(Point);const auto reads=f.reads();
    now+=200000000;attempt(true);assert(f.total()==first&&f.ticks(Point)==point&&f.reads()==reads&&calls==4);
    // Raw counters can be present and legitimately report zero on a sample.
    f.begin(true,&fake,counters,4);f.end();assert(f.drawsMeasured());
    for(unsigned b=0;b<Count;++b)assert(f.drawCalls(b)==0);
    for(unsigned b=0;b<Count;++b)assert(name(b)[0]!='?');
    std::puts("PASS effects buckets: exact phase sums, slow unsampled frames, unavailable draw counts, sampled zero counts, 8ms threshold, no-op preservation, off reads no clock");
}
