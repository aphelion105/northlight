#pragma once
#include <atomic>
#include <cstdint>
#include <cstddef>
#include <type_traits>

namespace NorthlightStream {
// Set once when the selected backend is identified. Only DXVK 3.1.1 has been
// audited here: pending GetData leaves BOOL untouched before its command-thread
// END runs, then writes FALSE while the Vulkan event is pending. Neither value
// is a completion result. Other runtimes keep their original calls unchanged.
inline std::atomic<bool> dxvk311EventPayload{false};
enum class EventPendingKind : unsigned {None,Untouched,FalsePayload,Ready,Other};
// Keep upper bytes zero: DXVK's cached EVENT path writes a C++ bool, not BOOL.
inline constexpr std::int32_t EventPayloadMarker=0x7e;
inline EventPendingKind eventPendingKind(HRESULT hr,std::int32_t payload){
    if(hr==S_OK)return EventPendingKind::Ready;
    if(hr!=S_FALSE)return EventPendingKind::Other;
    return payload==EventPayloadMarker?EventPendingKind::Untouched:payload==0?EventPendingKind::FalsePayload:EventPendingKind::Other;
}
// An untouched audited DXVK payload means its CPU command thread has not run
// END yet. Give a fast CPU completion 200 us before sleeping: the native timer
// can oversleep a 20 us request by hundreds of microseconds. Longer spinning
// competes with our producer. GPU pending and unaudited backends keep 2 ms.
template<class ShortPause,class LongPause>
inline void pauseEventQuery(EventPendingKind backend,bool expired,std::uint64_t left,std::uint64_t elapsed,ShortPause& shortPause,LongPause& longPause){
    if((backend==EventPendingKind::Untouched&&elapsed>=200000)||expired||elapsed>=2000000)longPause(left,elapsed);
    else shortPause(left,elapsed);
}
// Flush once to submit the fence, then periodically for runtimes that need
// another kick. Polling completion between kicks must not repeatedly flush
// unrelated command buffers. Each synchronous trip starts a fresh cadence.
struct EventFlushCadence {
    std::uint64_t previous=0;bool started=false;
    unsigned flags(std::uint64_t now){
        if(!started||now<previous||now-previous>=1000000){started=true;previous=now;return 1;}
        return 0;
    }
};
// Existing clock samples immediately around a yield, including loop bookkeeping
// but no GetData or queue work. Overshoot is observed scheduling/pause latency,
// never a measurement of GPU execution or the instant the fence became ready.
struct EventPauseProfile {
    // maxRequestedNs is the request paired with maxNs, not the largest request.
    std::uint64_t count=0,actualNs=0,overshootNs=0,maxNs=0,maxRequestedNs=0,maxOvershootNs=0;
    void record(std::uint64_t actual,std::uint64_t requested){
        ++count;actualNs+=actual;
        const auto over=actual>requested?actual-requested:0;overshootNs+=over;
        if(actual>maxNs){maxNs=actual;maxRequestedNs=requested;}
        if(over>maxOvershootNs)maxOvershootNs=over;
    }
    void add(const EventPauseProfile& other){
        count+=other.count;actualNs+=other.actualNs;overshootNs+=other.overshootNs;
        if(other.maxNs>maxNs){maxNs=other.maxNs;maxRequestedNs=other.maxRequestedNs;}
        if(other.maxOvershootNs>maxOvershootNs)maxOvershootNs=other.maxOvershootNs;
    }
};
// Game-thread diagnostics. The replay timestamps return through the existing
// synchronous task; these spans partition one trip and never imply GPU cost.
struct QueryWaitProfile {
    std::uint64_t calls=0,flush=0,unissued=0,pending=0,errors=0,waitNs=0;
    std::uint64_t beforeNs=0,replayNs=0,afterNs=0,tripNs=0,maxTripNs=0;
    std::uint64_t eventFastTrips=0,eventLongTrips=0,eventFastNs=0,eventLongNs=0;
    EventPauseProfile eventPauses;
    // Final backend observation per synchronous trip, not all polls in it.
    std::uint64_t backendUntouched=0,backendFalse=0,backendReady=0,backendOther=0;
    std::uint64_t fenceCalls=0,fenceNs=0,fenceTimeouts=0;
    // Several bounded EVENT trips can stall one frame even though each trip
    // stays below the slow-report threshold. Use the frame's accumulated time.
    bool frameReportDue(bool sampled)const{return calls&&(sampled||tripNs>=20000000);}
    void record(unsigned flags,bool neverIssued,bool ran,HRESULT result,std::uint64_t waited,
                std::uint64_t start,std::uint64_t replayStart,std::uint64_t replayEnd,std::uint64_t end,
                const EventPauseProfile* pauses=nullptr,bool longPhase=false,EventPendingKind backend=EventPendingKind::None){
        ++calls;flush+=bool(flags&1);unissued+=neverIssued;pending+=ran&&result==S_FALSE;errors+=!ran||result<0;waitNs+=waited;
        backendUntouched+=backend==EventPendingKind::Untouched;backendFalse+=backend==EventPendingKind::FalsePayload;
        backendReady+=backend==EventPendingKind::Ready;backendOther+=backend==EventPendingKind::Other;
        if(ran&&replayStart>=start&&replayEnd>=replayStart&&end>=replayEnd){
            beforeNs+=replayStart-start;replayNs+=replayEnd-replayStart;afterNs+=end-replayEnd;
            const auto trip=end-start;tripNs+=trip;if(trip>maxTripNs)maxTripNs=trip;
            if(pauses){eventPauses.add(*pauses);
                if(longPhase){++eventLongTrips;eventLongNs+=replayEnd-replayStart;}
                else{++eventFastTrips;eventFastNs+=replayEnd-replayStart;}}
        }
    }
};
// Amortize EVENT/FLUSH round trips without inventing completion or holding the replay thread indefinitely.
template<class Poll,class Clock>
inline HRESULT pollEventQueryBriefly(Poll&& poll,Clock&& now,unsigned& calls){
    calls=1;HRESULT hr=poll();
    if(hr!=S_FALSE)return hr;
    const std::uint64_t start=now();std::uint64_t previous=start;
    // Do not turn fewer round trips into a tight driver-poll storm. The check limit also bounds a stalled clock.
    for(unsigned checks=0;calls<64&&checks<4096;++checks){
        const std::uint64_t current=now();
        if(current-start>=50000)break;
        if(current-previous<2000)continue;
        previous=current;
        ++calls;hr=poll();
        if(hr!=S_FALSE)break;
    }
    return hr;
}
// A normal frame's EVENT wait must not re-enter the client's costly Sleep(0) retry
// path after only 2 ms. Return at completion; 16 ms bounds an unresponsive query.
inline constexpr std::uint64_t kEventFlushWaitNs=16000000;
// The pumped round trip of a pending EVENT/FLUSH (stream_proxies.h): D3D9 has no blocking
// query wait, so the replay thread holds the trip until the event completes or budgetNs passes, polling at a growing gap
// (20, 40, then at most 50 us between polls). Pause receives the remaining poll
// interval (clipped to the trip budget) and elapsed trip time. The game thread sleeps in
// its pumped wait meanwhile; a long GPU frame takes a few trips instead of one per driver call.
template<class Poll,class Clock,class Yield,class Block=std::nullptr_t>
inline HRESULT waitEventQuery(Poll&& poll,Clock&& now,Yield&& yield,unsigned& calls,std::uint64_t budgetNs=2000000,std::uint64_t* pendingNs=nullptr,EventPauseProfile* pauses=nullptr,Block block=nullptr){
    if(pendingNs)*pendingNs=0;
    if(pauses)*pauses={};
    calls=1;HRESULT hr=poll();
    if(hr!=S_FALSE)return hr;
    const std::uint64_t start=now();std::uint64_t gap=20000,next=start+gap,previous=start;
    std::uint64_t pauseStart=0,pauseRequested=0;bool pauseOpen=false;
    bool blockTried=false;
    for(unsigned stalled=0;stalled<(1u<<16);){
        const std::uint64_t current=now();
        if(current<previous)break;
        if(pauseOpen){pauses->record(current-pauseStart,pauseRequested);pauseOpen=false;}
        // A scheduled pause can return after the deadline. Check real completion
        // once before sending the client through another S_FALSE retry trip.
        if(current-start>=budgetNs){++calls;hr=poll();break;}
        // Count only a stalled clock. A fast yield must not exhaust the check
        // limit before the actual deadline (observed at 7-8 ms for a 16 ms wait).
        stalled=current==previous?stalled+1:0;previous=current;
        if(current<next){const auto left=next-current,budget=budgetNs-(current-start),requested=left<budget?left:budget;
            if(pauses){pauseStart=current;pauseRequested=requested;pauseOpen=true;}
            yield(requested,current-start);continue;}
        ++calls;hr=poll();
        if(hr!=S_FALSE)break;
        if constexpr(!std::is_same_v<Block,std::nullptr_t>){
            // A short query keeps its existing cadence. After a real pending
            // poll, permit one driver wait; its wake is never a query result.
            if(!blockTried&&current-start>=2000000){
                blockTried=true;const auto at=now();
                if(at>=start&&at-start<budgetNs)block(budgetNs-(at-start));
            }
        }
        gap=gap<25000?gap*2:50000;next=current+gap;
    }
    if(pendingNs&&hr>=0){const auto end=now();if(end>=start)*pendingNs=end-start;}
    return hr;
}
}
