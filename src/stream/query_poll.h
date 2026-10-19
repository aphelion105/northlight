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
}
