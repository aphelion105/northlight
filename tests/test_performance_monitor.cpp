#include "performance_monitor.h"
#include <cassert>
#include <cmath>
#include <cstdio>
int main(){
    using namespace NorthlightPerformance;History h;Frame f;f.at=1;f.streamed=true;
    h.add(0,1000,true,f);for(unsigned i=1;i<=1000;++i)h.add(i*10,1000,true,f);
    auto t=h.read();assert(t.samples==1000&&t.fps==100&&t.low1==100&&t.low01==100&&t.graphCount==240);
    f.eventWaitNs=141000000;f.queueIdleNs=9000000;h.add(10141,1000,true,f);t=h.read();assert(t.maxMs==141&&t.frame.eventWaitMs==141&&t.frame.queueIdleMs==9&&t.low01<20);
    const auto before=t.samples;h.add(20000,1000,false,f);h.add(25000,1000,true,f);assert(h.read().samples==before&&h.read().frame.eventWaitMs<0);
    h.add(25010,1000,true,f);assert(h.read().samples==before+1);h.add(30000,1000,true,f);assert(h.read().samples==before+1);
    h.reset();h.add(0,1000,true,f);for(unsigned i=1;i<99;++i)h.add(i*16,1000,true,f);assert(h.read().low1<0&&h.read().low01<0);
    h.add(99*16,1000,true,f);h.add(100*16,1000,true,f);assert(h.read().low1==62.5&&h.read().low01<0);
    for(unsigned i=101;i<6000;++i)h.add(i*16,1000,true,f);t=h.read();assert(t.samples==4096&&t.low01==62.5&&t.graph[239]==16);
    Store s;assert(!s.active());s.publish(1,1,1000,true,f);s.touch(1);assert(s.wanted(1)&&!s.wanted(2));s.publish(1,1000,1000,true,f);s.publish(1,1010,1000,true,f);assert(s.read(1,t)&&t.fps==100);
    for(unsigned i=1;i<=240;++i){s.publish(1,1010+i,1000,true,f);assert(s.graph(1,t)&&t.graph[t.graphCount-1]==1&&t.samples==i+1&&t.fps==100);}
    assert(s.read(1,t)&&t.fps>900);s.reset(1);assert(s.graph(1,t)&&!t.graphCount&&!t.samples);s.detach(1);assert(!s.wanted(1));
    std::puts("PERFORMANCE_MONITOR_PASS low-percent-means warmup=100/1000 diagnostic-focus-gap-breaks ring-wrap event-141ms counters owner-lease");
}
