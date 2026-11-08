#include <cstdint>
#include <cassert>
#include <cstdio>
#include <algorithm>
using HRESULT=std::int32_t;
constexpr HRESULT S_OK=0,S_FALSE=1,E_FAIL=-1;
#include "query_poll.h"
#include "stream_stats.h"
void replayAttribution(){
    NorthlightStream::ReplayFrameProfile p;
    p.begin(false,127);p.record(4,900);assert(!p.enabled&&!p.calls&&!p.totalNs);
    p.recordUpload(512,10,20,30,false,false,4);assert(!p.upload.calls&&!p.upload.clockReads);
    p.begin(true,126);p.record(4,900);assert(!p.enabled&&!p.calls);
    p.begin(true,127);assert(p.enabled&&p.frame==127);
    p.record(4,300);p.record(7,200);p.record(4,400);p.record(512,10000);
    assert(p.calls==3&&p.totalNs==900&&p.maxNs==400&&p.maxId==4);
    assert(p.entries[4].calls==2&&p.entries[4].ns==700&&p.entries[7].ns==200);
    p.recordUpload(256,10,20,30,false,false,4);p.recordUpload(128,40,0,0,true,false,2);p.recordUpload(64,15,0,0,false,true,2);
    assert(p.upload.calls==3&&p.upload.bytes==448&&p.upload.skipped==1&&p.upload.failed==1&&p.upload.clockReads==8);
    assert(p.upload.lockNs==65&&p.upload.copyNs==20&&p.upload.unlockNs==30&&p.upload.maxLockNs==40&&p.upload.maxCopyNs==20&&p.upload.maxUnlockNs==30);
    p.begin(true,254);assert(p.enabled&&!p.calls&&!p.totalNs&&!p.maxNs);
    assert(!p.upload.calls&&!p.upload.bytes&&!p.upload.clockReads);
    for(const auto& e:p.entries)assert(!e.calls&&!e.ns);
    p.record(511,1);assert(p.calls==1&&p.maxId==511);
    unsigned coverage[256]={};
    for(unsigned frame=1;frame<=16;++frame){
        p.begin(true,127*frame);unsigned timed=0;
        for(unsigned call=0;call<264;++call){const bool take=p.sample(4,true);timed+=take;if(call<8)assert(take);else coverage[call-8]+=take;if(take)p.record(4,10);}
        for(unsigned call=0;call<100;++call){assert(p.sample(7,false));p.record(7,20);}
        assert(timed==24&&p.calls==124&&p.observed==364&&p.entries[4].observed==264&&p.entries[4].calls==24);
        assert(p.totalNs==2240); // Actual measured subset, never extrapolated.
    }
    for(auto count:coverage)assert(count==1);
    p.begin(false,127);assert(!p.sample(4,true)&&!p.sample(7,false));
    p.begin(true,127);assert(!p.sample(512,true)&&p.observed==0);
    std::puts("REPLAY_PROFILE_PASS prefix=8 stride=16 rotatingCoverage=exact drawsUploadsWaits=full measuredSubset=unscaled");
}
void backendPendingAttribution(){
    using namespace NorthlightStream;
    assert(!dxvk311EventPayload.load());
    assert(eventPendingKind(S_FALSE,EventPayloadMarker)==EventPendingKind::Untouched);
    assert(eventPendingKind(S_FALSE,0)==EventPendingKind::FalsePayload);
    assert(eventPendingKind(S_OK,1)==EventPendingKind::Ready);
    assert(eventPendingKind(S_FALSE,1)==EventPendingKind::Other);
    assert(eventPendingKind(E_FAIL,EventPayloadMarker)==EventPendingKind::Other);
    QueryWaitProfile q;
    for(auto kind:{EventPendingKind::Untouched,EventPendingKind::FalsePayload,EventPendingKind::Ready,EventPendingKind::Other})
        q.record(1,false,true,S_FALSE,16000000,0,0,16000000,16000000,nullptr,false,kind);
    assert(q.backendUntouched==1&&q.backendFalse==1&&q.backendReady==1&&q.backendOther==1);
    assert(q.calls==4&&q.pending==4&&q.tripNs==64000000); // A diagnostic ready snapshot never replaces the original status.
    puts("EVENT_BACKEND_PROBE_PASS payload_observations_only original_status_unchanged");
}
void backendPausePolicy(){
    using namespace NorthlightStream;
    // Execute the production selector and wait loop. A cached/immediate result
    // performs no pause; CS pending keeps only a 200 us grace, also unprofiled.
    for(bool profile:{false,true})for(auto backend:{EventPendingKind::Untouched,EventPendingKind::FalsePayload,EventPendingKind::None}){
        std::uint64_t clock=0;unsigned calls=0,shortCalls=0,longCalls=0;
        auto shortPause=[&](std::uint64_t left,std::uint64_t){++shortCalls;clock+=left;};
        auto longPause=[&](std::uint64_t left,std::uint64_t){++longCalls;clock+=left;};
        auto pause=[&](std::uint64_t left,std::uint64_t elapsed){pauseEventQuery(backend,false,left,elapsed,shortPause,longPause);};
        EventPauseProfile p;
        assert(waitEventQuery([]{return S_OK;},[&]{return clock;},pause,calls,kEventFlushWaitNs,nullptr,profile?&p:nullptr)==S_OK);
        assert(calls==1&&!shortCalls&&!longCalls&&clock==0);
        assert(waitEventQuery([&]{return clock>=100000?S_OK:S_FALSE;},[&]{return clock;},pause,calls,kEventFlushWaitNs,nullptr,profile?&p:nullptr)==S_OK);
        assert(clock==110000&&calls==4);
        assert(shortCalls==3&&!longCalls);
        shortCalls=longCalls=0;
        pauseEventQuery(backend,false,100,199999,shortPause,longPause);
        assert(shortCalls==1&&!longCalls);
        pauseEventQuery(backend,false,100,200000,shortPause,longPause);
        if(backend==EventPendingKind::Untouched)assert(shortCalls==1&&longCalls==1);
        else assert(shortCalls==2&&!longCalls);
        shortCalls=longCalls=0;
        pauseEventQuery(backend,true,100,0,shortPause,longPause);
        pauseEventQuery(backend,false,100,2000000,shortPause,longPause);
        assert(!shortCalls&&longCalls==2);
    }
    puts("EVENT_CS_PAUSE_PASS production_selector ready_no_pause cs_grace_200us_exact gpu_grace_2ms profiling_off_on expired_immediate_sleep");
}
void blockingWaits(){
    using namespace NorthlightStream;
    for(HRESULT terminal:{S_OK,S_FALSE,E_FAIL}){
        std::uint64_t clock=0;unsigned calls=0,blocks=0;
        auto poll=[&]{return clock>=8000000?terminal:S_FALSE;};
        auto block=[&](std::uint64_t left){++blocks;assert(clock>=2000000&&left<=14000000);clock=8000000;};
        const auto result=waitEventQuery(poll,[&]{return clock;},[&](std::uint64_t left,std::uint64_t){clock+=left;},calls,kEventFlushWaitNs,nullptr,nullptr,block);
        assert(result==terminal&&blocks==1);if(terminal==S_FALSE)assert(clock==kEventFlushWaitNs);
    }
    for(std::uint64_t ready:{0u,1000000u,1990000u}){
        std::uint64_t clock=0;unsigned calls=0;
        assert(waitEventQuery([&]{return clock>=ready?S_OK:S_FALSE;},[&]{return clock;},[&](std::uint64_t left,std::uint64_t){clock+=left;},calls,kEventFlushWaitNs,nullptr,nullptr,[](std::uint64_t){assert(false);})==S_OK);
    }
    std::puts("QUERY_NATIVE_BLOCK_PASS real_poll_controls_completion short_unchanged once_per_trip pending_error_deadline");
}
void waitAttribution(){
    backendPendingAttribution();blockingWaits();
    // Operational GPU attribution excludes the queue trip, immediate answers
    // and failed queries; it also works without QueryWaitProfile diagnostics.
    std::uint64_t pending=99,clock=1000000;unsigned calls=0;
    const auto now=[&]{clock+=1000;return clock;};
    assert(NorthlightStream::waitEventQuery([]{return S_OK;},now,[](std::uint64_t,std::uint64_t){},calls,2000000,&pending)==S_OK&&pending==0&&clock==1000000);
    assert(NorthlightStream::waitEventQuery([&]{return clock>=1400000?S_OK:S_FALSE;},now,[](std::uint64_t,std::uint64_t){},calls,2000000,&pending)==S_OK);
    assert(pending>=400000&&pending<452000);
    clock=0;
    assert(NorthlightStream::waitEventQuery([]{return S_FALSE;},now,[](std::uint64_t,std::uint64_t){},calls,2000000,&pending)==S_FALSE&&pending>=2000000&&pending<2002000);
    clock=0;
    assert(NorthlightStream::waitEventQuery([&]{return clock>=400000?E_FAIL:S_FALSE;},now,[](std::uint64_t,std::uint64_t){},calls,2000000,&pending)==E_FAIL&&pending==0);
    assert(NorthlightStream::waitEventQuery([]{return S_FALSE;},[]{return std::uint64_t(0);},[](std::uint64_t,std::uint64_t){},calls,2000000,&pending)==S_FALSE&&pending==0);
    NorthlightStream::QueryWaitProfile s;
    s.record(1,false,true,S_OK,780,100,200,800,1000);
    assert(s.calls==1&&s.flush==1&&!s.errors&&!s.pending&&!s.unissued&&s.waitNs==780);
    assert(s.beforeNs==100&&s.replayNs==600&&s.afterNs==200&&s.tripNs==900&&s.maxTripNs==900);
    s.record(0,true,true,S_FALSE,40,2000,2005,2040,2050);
    assert(s.calls==2&&s.flush==1&&s.unissued==1&&s.pending==1&&!s.errors&&s.waitNs==820);
    assert(s.beforeNs+s.replayNs+s.afterNs==s.tripNs&&s.tripNs==950&&s.maxTripNs==900);
    // Nested/refused tasks have no replay timestamps. A backwards clock cannot
    // turn into unsigned multi-year latency; status/call counts remain visible.
    s.record(1,false,false,E_FAIL,0,3000,0,0,3010);
    s.record(1,false,true,E_FAIL,12,4000,3999,4010,4020);
    s.record(1,false,true,S_OK,0,5000,5002,5001,5003);
    s.record(1,false,true,S_OK,0,6000,6001,6002,6000);
    assert(s.calls==6&&s.errors==2&&s.tripNs==950&&s.maxTripNs==900);
    s={};assert(!s.calls&&!s.waitNs&&!s.tripNs&&!s.maxTripNs);
    assert(!s.frameReportDue(false)&&!s.frameReportDue(true));
    s.record(1,false,true,S_FALSE,16000000,0,0,16000000,16000000);
    assert(!s.frameReportDue(false)&&s.frameReportDue(true));
    s.record(1,false,true,S_OK,16000000,16000000,16000000,32000000,32000000);
    assert(s.tripNs==32000000&&s.maxTripNs==16000000&&s.frameReportDue(false));
    s={};s.record(1,false,true,S_OK,19999999,0,0,19999999,19999999);assert(!s.frameReportDue(false));
    s={};s.record(1,false,true,S_OK,20000000,0,0,20000000,20000000);assert(s.frameReportDue(false));
    std::puts("QUERY_FRAME_REPORT_PASS accumulated_trips exact_threshold empty_and_sampled");
}
void scheduledPauses(){
    using namespace NorthlightStream;
    std::uint64_t clock=0;unsigned calls=0,pauses=0;
    auto pause=[&](std::uint64_t left,std::uint64_t elapsed){
        assert(left>0&&left<=50000&&elapsed<kEventFlushWaitNs);++pauses;clock+=left;
    };
    assert(waitEventQuery([]{return S_OK;},[&]{return clock;},pause,calls,kEventFlushWaitNs)==S_OK&&calls==1&&!pauses);
    // Timer wakes need not coincide with the requested deadline. Completion,
    // errors and still-pending status must all come from one final real poll.
    for(HRESULT terminal:{S_OK,E_FAIL,S_FALSE}){
        clock=0;pauses=0;
        EventPauseProfile observed;
        auto oversleep=[&](std::uint64_t left,std::uint64_t elapsed){
            assert(left==20000&&elapsed==0);++pauses;clock=kEventFlushWaitNs+100000;
        };
        assert(waitEventQuery([&]{return clock?terminal:S_FALSE;},[&]{return clock;},oversleep,calls,kEventFlushWaitNs,nullptr,&observed)==terminal);
        assert(pauses==1&&calls==2);
        assert(observed.count==1&&observed.maxNs==kEventFlushWaitNs+100000&&observed.maxRequestedNs==20000);
    }
    clock=0;pauses=0;
    assert(waitEventQuery([]{return S_FALSE;},[&]{return clock;},pause,calls,75000)==S_FALSE);
    assert(clock==75000&&calls==4&&pauses==3); // Last 50us interval clipped to the remaining 15us budget.
    std::puts("QUERY_PAUSE_PASS immediate_no_pause oversleep_real_completion_error_pending clipped_deadline");
}
void observedPauses(){
    using namespace NorthlightStream;
    {EventPauseProfile p;p.count=p.actualNs=99;unsigned calls=0,clocks=0;
     assert(waitEventQuery([]{return S_OK;},[&]{++clocks;return std::uint64_t(0);},[](std::uint64_t,std::uint64_t){assert(false);},calls,kEventFlushWaitNs,nullptr,&p)==S_OK);
     assert(!clocks&&calls==1&&!p.count&&!p.actualNs);}
    struct Run {HRESULT hr=S_FALSE;std::uint64_t clock=0,pending=0;unsigned clocks=0,polls=0,yields=0;EventPauseProfile pauses;};
    const auto run=[](unsigned mode,bool profile){
        Run r;if(mode==4)r.clock=1000;
        auto now=[&]{++r.clocks;return r.clock;};
        auto poll=[&]{++r.polls;if(mode==5)r.clock+=500000;return mode==3||mode==4?S_FALSE:r.clock>=(mode==5?2000000:100000)?S_OK:S_FALSE;};
        auto pause=[&](std::uint64_t requested,std::uint64_t){++r.yields;
            if(mode==0||mode==5)r.clock+=requested;
            if(mode==1)r.clock+=requested+8000000;
            if(mode==2)r.clock=kEventFlushWaitNs+100000;
            if(mode==4)--r.clock;
        };
        unsigned calls=0;r.hr=waitEventQuery(poll,now,pause,calls,kEventFlushWaitNs,&r.pending,profile?&r.pauses:nullptr);
        assert(calls==r.polls);return r;
    };
    for(unsigned mode=0;mode<6;++mode){const auto plain=run(mode,false),measured=run(mode,true);
        // Instrumentation uses exactly the existing samples and never changes
        // a real poll, pause, deadline, completion result or operational wait.
        assert(plain.hr==measured.hr&&plain.clock==measured.clock&&plain.pending==measured.pending);
        assert(plain.clocks==measured.clocks&&plain.polls==measured.polls&&plain.yields==measured.yields);
        const auto& p=measured.pauses;
        if(mode==0)assert(p.count==3&&p.actualNs==110000&&!p.overshootNs&&p.maxNs==50000&&p.maxRequestedNs==50000);
        if(mode==1)assert(p.count==1&&p.actualNs==8020000&&p.overshootNs==8000000&&p.maxNs==8020000&&p.maxRequestedNs==20000&&p.maxOvershootNs==8000000);
        if(mode==2)assert(p.count==1&&p.actualNs==16100000&&p.overshootNs==16080000&&p.maxRequestedNs==20000);
        if(mode==3)assert(!p.actualNs&&!p.overshootNs&&!p.maxNs&&p.count+1==measured.yields); // stopped clock, bounded guard
        if(mode==4)assert(!p.count&&!p.actualNs&&!p.overshootNs); // regressed sample is not an unsigned duration
        if(mode==5)assert(p.count==1&&p.actualNs==20000&&!p.overshootNs&&measured.clock>2000000); // excludes the 0.5ms GetData calls
    }
    const auto fast=run(1,true),longer=run(2,true);QueryWaitProfile frame;
    frame.record(1,false,true,S_FALSE,fast.clock,0,0,fast.clock,fast.clock,&fast.pauses,false);
    frame.record(1,false,true,S_OK,longer.clock,0,0,longer.clock,longer.clock,&longer.pauses,true);
    assert(frame.eventFastTrips==1&&frame.eventLongTrips==1&&frame.eventFastNs==fast.clock&&frame.eventLongNs==longer.clock);
    assert(frame.eventPauses.count==2&&frame.eventPauses.actualNs==fast.pauses.actualNs+longer.pauses.actualNs);
    assert(frame.eventPauses.maxNs==16100000&&frame.eventPauses.maxRequestedNs==20000&&frame.eventPauses.maxOvershootNs==16080000);
    const auto before=frame.eventPauses.actualNs;
    frame.record(1,false,false,E_FAIL,0,0,0,0,0,&longer.pauses,true);
    frame.record(1,false,true,S_FALSE,0,5,4,6,7,&longer.pauses,true);
    assert(frame.eventPauses.actualNs==before&&frame.eventLongTrips==1);
    frame={};assert(!frame.eventFastTrips&&!frame.eventLongTrips&&!frame.eventPauses.count);
    std::puts("QUERY_PAUSE_PROFILE_PASS no_extra_clock_reads unchanged_polls_and_results oversleep_fast_long excludes_driver_time clock_stop_regress");
}
int main(){
    backendPausePolicy();
    scheduledPauses();
    observedPauses();
    {
        NorthlightStream::EventFlushCadence cadence;
        unsigned flushes=0;
        for(std::uint64_t now=0;now<16000000;now+=50000)flushes+=cadence.flags(now);
        assert(flushes==16); // Previously 320 driver flush requests.
        assert(cadence.flags(0)==1&&cadence.flags(0)==0); // clock rollback/stall
        NorthlightStream::EventFlushCadence fresh;
        assert(fresh.flags(0)==1); // retry/new generation starts with a flush
    }
    replayAttribution();
    waitAttribution();
    std::uint64_t worst=0;unsigned maxCalls=0;
    for(std::uint64_t ready=0;ready<1700000;ready+=1000){
        std::uint64_t clock=0;unsigned calls=0;
        const auto hr=NorthlightStream::waitEventQuery([&]{return clock>=ready?S_OK:S_FALSE;},[&]{clock+=1000;return clock;},[](std::uint64_t,std::uint64_t){},calls);
        assert(hr==S_OK&&clock>=ready);worst=std::max(worst,clock-ready);maxCalls=std::max(maxCalls,calls);
    }
    std::uint64_t clock=0;unsigned calls=0;
    assert(NorthlightStream::waitEventQuery([]{return S_FALSE;},[&]{clock+=1000;return clock;},[](std::uint64_t,std::uint64_t){},calls)==S_FALSE);
    assert(clock>=2000000&&clock<2002000&&calls<=45);
#ifndef NORTHLIGHT_QUERY_LATENCY_BASELINE
    assert(worst<=52000);
#endif
    assert(NorthlightStream::waitEventQuery([]{return E_FAIL;},[]{return std::uint64_t(0);},[](std::uint64_t,std::uint64_t){},calls)==E_FAIL&&calls==1);
    assert(NorthlightStream::waitEventQuery([]{return S_FALSE;},[]{return std::uint64_t(0);},[](std::uint64_t,std::uint64_t){},calls)==S_FALSE&&calls==1);
    std::printf("QUERY_POLL_PASS readySweep=1700 worstDetectionNs=%llu maxDriverPolls=%u pendingBudgetMs=2 error=preserved stalledClock=bounded\n",static_cast<unsigned long long>(worst),maxCalls);
    // Normal GPU completion after the old 2 ms deadline must not escape into
    // the game's Sleep(0) loop. Exercise the production FLUSH budget separately.
    worst=0;maxCalls=0;
    for(std::uint64_t ready=0;ready<15900000;ready+=1000){
        clock=0;
        const auto hr=NorthlightStream::waitEventQuery([&]{return clock>=ready?S_OK:S_FALSE;},[&]{clock+=1000;return clock;},[](std::uint64_t,std::uint64_t){},calls,NorthlightStream::kEventFlushWaitNs);
        assert(hr==S_OK&&clock>=ready);worst=std::max(worst,clock-ready);maxCalls=std::max(maxCalls,calls);
    }
    assert(worst<=52000&&maxCalls<=322);
    clock=0;
    assert(NorthlightStream::waitEventQuery([&]{return clock>=12000000?S_OK:S_FALSE;},[&]{clock+=100;return clock;},[](std::uint64_t,std::uint64_t){},calls,NorthlightStream::kEventFlushWaitNs)==S_OK);
    assert(clock>=12000000&&clock<12050200&&calls<=245); // More than 65,536 live clock checks must not expire the wait.
    clock=0;
    assert(NorthlightStream::waitEventQuery([]{return S_FALSE;},[&]{clock+=1000;return clock;},[](std::uint64_t,std::uint64_t){},calls,NorthlightStream::kEventFlushWaitNs)==S_FALSE);
    assert(clock>=16000000&&clock<16002000&&calls<=326);
    assert(NorthlightStream::waitEventQuery([]{return S_FALSE;},[]{return std::uint64_t(0);},[](std::uint64_t,std::uint64_t){},calls,NorthlightStream::kEventFlushWaitNs)==S_FALSE&&calls==1);
    clock=0;
    assert(NorthlightStream::waitEventQuery([&]{return clock>=6000000?E_FAIL:S_FALSE;},[&]{clock+=1000;return clock;},[](std::uint64_t,std::uint64_t){},calls,NorthlightStream::kEventFlushWaitNs)==E_FAIL);
    assert(clock>=6000000&&clock<6052000);
    std::printf("QUERY_FLUSH_PASS readySweep=15900 worstDetectionNs=%llu maxDriverPolls=%u pendingBudgetMs=16 delayedError=preserved stalledClock=bounded\n",static_cast<unsigned long long>(worst),maxCalls);
}
