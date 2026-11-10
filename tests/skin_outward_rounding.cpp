#define NOMINMAX
#include <d3d9.h>
#include <cassert>
#include <cerrno>
#include <cfenv>
#include <cstdio>
#include <random>
#include "replay_bounds.h"
#pragma STDC FENV_ACCESS ON

static std::uint32_t bits(float value){std::uint32_t out;std::memcpy(&out,&value,4);return out;}
int main(){
    std::fenv_t saved;assert(std::fegetenv(&saved)==0);
#if defined(__SSE2__)
    const auto savedMxcsr=_mm_getcsr();
#endif
    std::vector<std::uint32_t> patterns={0,0x80000000u,1,0x80000001u,0x007fffffu,0x807fffffu,0x00800000u,0x80800000u,0x00800001u,0x80800001u,0x7f7ffffeu,0xff7ffffeu,0x7f7fffffu,0xff7fffffu,0x7f800000u,0xff800000u,0x7fc01234u,0xffc01234u,0x7f801234u,0xff801234u};
    for(unsigned exponent=1;exponent<255;++exponent)for(int neighbor:{-1,0,1})for(unsigned sign:{0u,0x80000000u})patterns.push_back(sign|std::uint32_t((exponent<<23)+neighbor));
    std::mt19937 random(0x9d39);for(unsigned i=0;i<4096;++i)patterns.push_back(random());
    using Next=float(*)(float,float);Next volatile next=static_cast<Next>(&std::nextafter);
    unsigned checks=0;
    for(int mode:{FE_TONEAREST,FE_DOWNWARD,FE_UPWARD,FE_TOWARDZERO})for(unsigned flushing=0;flushing<4;++flushing){
        assert(std::fesetround(mode)==0);
#if defined(__SSE2__)
        _mm_setcsr((_mm_getcsr()&~0x8040u)|((flushing&1)?0x8000u:0u)|((flushing&2)?0x40u:0u));
#endif
        for(auto raw:patterns){float value;std::memcpy(&value,&raw,4);volatile float input=value;
            for(bool upper:{false,true})for(bool preset:{false,true}){
                assert(std::feclearexcept(FE_ALL_EXCEPT)==0);if(preset)assert(std::feraiseexcept(FE_INVALID|FE_INEXACT)==0);errno=preset?EDOM:0;
                const float expected=next(input,upper?INFINITY:-INFINITY);const int expectedErrno=errno,expectedFlags=std::fetestexcept(FE_ALL_EXCEPT);const auto expectedBits=bits(expected);
                assert(std::feclearexcept(FE_ALL_EXCEPT)==0);if(preset)assert(std::feraiseexcept(FE_INVALID|FE_INEXACT)==0);errno=preset?EDOM:0;
                const float actual=upper?NorthlightReplayBounds::detail::outwardNormal<true>(input):NorthlightReplayBounds::detail::outwardNormal<false>(input);
                const int actualErrno=errno,actualFlags=std::fetestexcept(FE_ALL_EXCEPT);const auto actualBits=bits(actual);
                if(actualBits!=expectedBits||actualErrno!=expectedErrno||actualFlags!=expectedFlags)std::printf("OUTWARD_DIFF input=%08x upper=%u round=%d flushing=%u preset=%u bits=%08x/%08x errno=%d/%d flags=%x/%x\n",raw,unsigned(upper),mode,flushing,unsigned(preset),expectedBits,actualBits,expectedErrno,actualErrno,expectedFlags,actualFlags);
                assert(actualBits==expectedBits&&actualErrno==expectedErrno&&actualFlags==expectedFlags);++checks;
            }
        }
    }
    assert(std::fesetenv(&saved)==0);
#if defined(__SSE2__)
    _mm_setcsr(savedMxcsr);
#endif
    std::printf("SKIN_OUTWARD_ROUNDING_PASS cases=%u bits=exact errno=exact flags=exact rounding=4 flushing=4 boundaries=1 nan=1 preset=1\n",checks);
}
