#pragma once
#include <cstdint>

namespace NorthlightStream {
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
// The pumped round trip of an EVENT that stayed pending past the local window (stream_proxies.h): D3D9 has no blocking
// query wait, so the replay thread holds the trip until the event completes or budgetNs passes, polling at a growing gap
// (20, 40, then at most 50 us between polls) and yielding the core in between. The game thread sleeps in
// its pumped wait meanwhile; a long GPU frame takes a few trips instead of one per driver call.
template<class Poll,class Clock,class Yield>
inline HRESULT waitEventQuery(Poll&& poll,Clock&& now,Yield&& yield,unsigned& calls,std::uint64_t budgetNs=2000000){
    calls=1;HRESULT hr=poll();
    if(hr!=S_FALSE)return hr;
    const std::uint64_t start=now();std::uint64_t gap=20000,next=start+gap;
    for(unsigned checks=0;checks<(1u<<16);++checks){   // the check limit also bounds a stalled clock
        const std::uint64_t current=now();
        if(current-start>=budgetNs)break;
        if(current<next){yield();continue;}
        ++calls;hr=poll();
        if(hr!=S_FALSE)break;
        gap=gap<25000?gap*2:50000;next=current+gap;
    }
    return hr;
}
}
