#include <cstdint>
#include <cassert>
#include <cstdio>
#include <algorithm>
using HRESULT=std::int32_t;
constexpr HRESULT S_OK=0,S_FALSE=1,E_FAIL=-1;
#include "query_poll.h"
int main(){
    std::uint64_t worst=0;unsigned maxCalls=0;
    for(std::uint64_t ready=0;ready<1700000;ready+=1000){
        std::uint64_t clock=0;unsigned calls=0;
        const auto hr=NorthlightStream::waitEventQuery([&]{return clock>=ready?S_OK:S_FALSE;},[&]{clock+=1000;return clock;},[]{},calls);
        assert(hr==S_OK&&clock>=ready);worst=std::max(worst,clock-ready);maxCalls=std::max(maxCalls,calls);
    }
    std::uint64_t clock=0;unsigned calls=0;
    assert(NorthlightStream::waitEventQuery([]{return S_FALSE;},[&]{clock+=1000;return clock;},[]{},calls)==S_FALSE);
    assert(clock>=2000000&&clock<2002000&&calls<=45);
#ifndef NORTHLIGHT_QUERY_LATENCY_BASELINE
    assert(worst<=52000);
#endif
    assert(NorthlightStream::waitEventQuery([]{return E_FAIL;},[]{return std::uint64_t(0);},[]{},calls)==E_FAIL&&calls==1);
    assert(NorthlightStream::waitEventQuery([]{return S_FALSE;},[]{return std::uint64_t(0);},[]{},calls)==S_FALSE&&calls==1);
    std::printf("QUERY_POLL_PASS readySweep=1700 worstDetectionNs=%llu maxDriverPolls=%u pendingBudgetMs=2 error=preserved stalledClock=bounded\n",static_cast<unsigned long long>(worst),maxCalls);
}
