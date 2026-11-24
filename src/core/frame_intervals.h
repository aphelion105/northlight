#pragma once
#include <algorithm>
#include <array>
#include <charconv>
#include <cstdint>

// CPU arrival intervals at Present, not GPU timings. Fixed storage, no waits.
// Reset across device resets and effect-mode changes so A/B windows do not mix.
namespace NorthlightFrameIntervals {
inline constexpr unsigned Capacity=120;
struct Report {
    unsigned count=0;
    double meanMs=0,p50Ms=0,p95Ms=0,maxMs=0;
    int64_t frequency=0;
    uint32_t lastFrame=0;
    // Borrowed through the next sample. The window never sorts these ticks.
    const uint64_t* deltas=nullptr;
    std::array<char,Capacity*21> csv()const{
        std::array<char,Capacity*21> text{};
        auto* cursor=text.data();
        for(unsigned i=0;i<count;++i){
            if(i)*cursor++=',';
            const auto field=std::to_chars(cursor,text.data()+text.size()-1,deltas[i]);
            if(field.ec!=std::errc{})return {};
            cursor=field.ptr;
        }
        *cursor='\0';return text;
    }
};
class Window {
    std::array<uint64_t,Capacity> deltas_{};
    unsigned count_=0;
    int64_t previous_=0,frequency_=0;
    uint32_t previousFrame_=0;
    bool primed_=false;
    void summarize(Report& out)const{
        out.count=count_;
        out.frequency=frequency_;
        out.lastFrame=previousFrame_;
        out.deltas=deltas_.data();
        const auto toMs=[&](uint64_t value){return double(value)*1000.0/double(frequency_);};
        for(unsigned i=0;i<count_;++i)out.meanMs+=toMs(deltas_[i]);
        out.meanMs/=count_;
        auto sorted=deltas_;
        std::sort(sorted.begin(),sorted.begin()+count_);
        out.p50Ms=(toMs(sorted[(count_-1)/2])+toMs(sorted[count_/2]))*.5;
        out.p95Ms=toMs(sorted[(count_*95+99)/100-1]);out.maxMs=toMs(sorted[count_-1]);
    }
public:
    void reset(){count_=0;primed_=false;}
    // Preserve short A/B segments before resetting at a settings boundary.
    // The next sample primes a new interval, so no interval crosses modes.
    bool takePartial(Report& out){
        out={};
        if(!count_){reset();return false;}
        summarize(out);
        reset();return true;
    }
    bool sample(int64_t tick,int64_t frequency,uint32_t frame,Report& out){
        out={};
        if(frequency<=0){reset();return false;}
        if(!primed_||frequency_!=frequency||frame-previousFrame_!=1||tick<=previous_){
            count_=0;previous_=tick;previousFrame_=frame;frequency_=frequency;primed_=true;
            return false;
        }
        deltas_[count_++]=uint64_t(tick)-uint64_t(previous_);
        previous_=tick;previousFrame_=frame;
        if(count_!=Capacity)return false;
        summarize(out);
        count_=0;return true;
    }
};
}
