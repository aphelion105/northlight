#include <windows.h>
#include <atomic>
#include <cassert>
#include <cstdio>
#include <thread>
#include "query_poll.h"
#include "stream_wait.h"
using namespace NorthlightStream;
int main(){
    DWORD_PTR process=0,system=0;assert(GetProcessAffinityMask(GetCurrentProcess(),&process,&system));
    const auto cpu=process&(~process+1);
    // Keep the process multicore (the production spin eligibility check), but
    // colocate these two threads as the scheduler is permitted to do.
    assert(SetThreadAffinityMask(GetCurrentThread(),cpu));
    std::uint64_t cycles[2]{},elapsed[2]{},detection[2]{},spins[2]{},sleeps[2]{};
    unsigned trips[2]{};
    for(unsigned run=0;run<48;++run){
        const unsigned mode=run%2;
        std::atomic<bool> begin{false},done{false};std::atomic<std::uint64_t> readyAt{0};
        std::thread backend([&]{
            assert(SetThreadAffinityMask(GetCurrentThread(),cpu));
            while(!begin.load(std::memory_order_acquire))std::this_thread::yield();
            // Required prior CPU work, independent of polling frequency.
            volatile unsigned work=7;
            for(unsigned n=0;n<600000;++n)work=work*1664525u+1013904223u;
            assert(work!=7);readyAt.store(nowNs());done.store(true,std::memory_order_release);
        });
        QueryLongPause longPause;QueryPollPause shortPause;
        auto shortWait=[&](std::uint64_t left,std::uint64_t elapsed){if(mode)assert(elapsed<200000);++spins[mode];shortPause(left,elapsed);};
        auto longWait=[&](std::uint64_t left,std::uint64_t elapsed){++sleeps[mode];longPause(left,elapsed);};
        BOOL value=EventPayloadMarker;EventPendingKind kind=EventPendingKind::None;
        auto poll=[&]{
            begin.store(true,std::memory_order_release);value=EventPayloadMarker;
            const auto hr=done.load(std::memory_order_acquire)?S_OK:S_FALSE;
            if(hr==S_OK)value=TRUE;
            kind=eventPendingKind(hr,value);return hr;
        };
        ULONGLONG before=0,after=0;assert(QueryThreadCycleTime(GetCurrentThread(),&before));
        const auto start=nowNs();HRESULT result=S_FALSE;bool expired=false;
        while(result==S_FALSE){
            longPause.begin();unsigned calls=0;++trips[mode];
            auto pause=[&](std::uint64_t left,std::uint64_t elapsed){
                // Neutralizing only the backend observation is the old-policy
                // control; both modes execute the same production scheduler.
                pauseEventQuery(mode?kind:EventPendingKind::None,expired,left,elapsed,shortWait,longWait);
            };
            result=waitEventQuery(poll,nowNs,pause,calls,kEventFlushWaitNs);
            expired=result==S_FALSE;assert(nowNs()-start<2000000000);
        }
        const auto end=nowNs();assert(QueryThreadCycleTime(GetCurrentThread(),&after));
        assert(result==S_OK&&value==TRUE);backend.join();
        cycles[mode]+=after-before;elapsed[mode]+=end-start;detection[mode]+=end-readyAt.load();
    }
    assert(spins[1]&&sleeps[1]);
    if(process&(process-1))assert(spins[0]);
    for(unsigned mode=0;mode<2;++mode)std::printf("EVENT_CS_SCHEDULER mode=%s runs=24 realPayload=TRUE cycles=%llu waitMs=%.4f detectionMs=%.4f shortPauses=%llu timerPauses=%llu trips=%u\n",
        mode?"cs-aware":"old-control",(unsigned long long)cycles[mode],double(elapsed[mode])/1e6,double(detection[mode])/1e6,
        (unsigned long long)spins[mode],(unsigned long long)sleeps[mode],trips[mode]);
    puts("EVENT_CS_SCHEDULER_PASS same_cpu_backend_progress production_wait_and_pause no_game_no_gpu");
}
