#include "frame_intervals.h"
#include <cassert>
#include <cstdio>
#include <cstring>
int main(){
    NorthlightFrameIntervals::Window w;NorthlightFrameIntervals::Report r;
    uint32_t present=0;
    const auto sample=[&](int64_t tick,int64_t hz){return w.sample(tick,hz,present++,r);};
    assert(!sample(0,1000));
    for(int i=1;i<=120;++i)assert(sample(i*10,1000)==(i==120));
    assert(r.count==120&&r.meanMs==10&&r.p50Ms==10&&r.p95Ms==10&&r.maxMs==10);
    assert(r.frequency==1000&&r.lastFrame==120);
    // A/B reset must discard the partial old-mode interval window.
    for(int i=1;i<=50;++i)assert(!sample(1200+i*10,1000));
    w.reset();assert(!sample(5000,1000));
    for(int i=1;i<=120;++i)assert(sample(5000+i*20,1000)==(i==120));
    assert(r.meanMs==20&&r.p95Ms==20);
    // Device/clock discontinuities never turn into huge unsigned intervals.
    assert(!sample(0,1000));assert(!sample(10,0));
    assert(!sample(0,1000));
    int64_t tick=0;
    for(int i=1;i<=120;++i){tick+=i;assert(sample(tick,1000)==(i==120));}
    assert(r.meanMs==60.5&&r.p50Ms==60.5&&r.p95Ms==114&&r.maxMs==120);
    // Brief OFF segments must be observable without mixing the next ON frame.
    assert(!sample(tick+2,1000));assert(!sample(tick+12,1000));
    assert(!sample(tick+18,1000));assert(w.takePartial(r));
    assert(r.count==3&&r.meanMs==6&&r.p50Ms==6&&r.p95Ms==10&&r.maxMs==10);
    assert(r.lastFrame==present-1&&std::strcmp(r.csv().data(),"2,10,6")==0);
    assert(!w.takePartial(r)&&r.count==0);
    assert(!sample(100000,1000));assert(!sample(100007,1000));
    assert(w.takePartial(r)&&r.count==1&&r.meanMs==7&&r.p50Ms==7&&r.p95Ms==7);
    assert(!sample(200000,1000));
    for(int i=1;i<=120;++i)assert(sample(200000+i*4,1000)==(i==120));
    assert(r.count==120&&r.meanMs==4);assert(!w.takePartial(r));
    // A missed Present or a changed QPC frequency ends the current window.
    assert(!sample(300000,1000));assert(!sample(300009,1000));
    ++present;
    assert(!sample(300020,1000));assert(!sample(300032,1000));
    assert(!sample(300040,2000));assert(!sample(300054,2000));
    assert(w.takePartial(r)&&r.count==1&&r.frequency==2000&&r.meanMs==7);
    assert(std::strcmp(r.csv().data(),"14")==0);
    std::puts("PASS Present interval statistics, mode/reset isolation, clock discontinuity");
}
