#pragma once
#include <cmath>
#include <cstdint>

namespace NorthlightAdtBounds {
// Owned values for the ordered draw. The render thread never reads client objects.
struct Evidence {
    float low[3]{},high[3]{};
    std::uint64_t producerFrame=0;

    bool contains(const float* submittedLow,const float* submittedHigh)const{
        for(unsigned axis=0;axis<3;++axis){
            const float tolerance=axis==2?.002f:.03f;
            if(!std::isfinite(submittedLow[axis])||!std::isfinite(submittedHigh[axis])||
               submittedLow[axis]>submittedHigh[axis]||submittedLow[axis]<low[axis]-tolerance||
               submittedHigh[axis]>high[axis]+tolerance)return false;
        }
        return true;
    }
};

// Keep exact per-producer-frame counts and one example for investigating a miss.
struct Coverage {
    using Log=void(*)(const char*,...);
    Evidence firstMismatch{};
    float submittedLow[3]{},submittedHigh[3]{};
    std::uint64_t producerFrame=0;
    unsigned compared=0,contained=0;

    void flush(Log log){
        if(!compared)return;
        log("NATIVE_ADT_COVERAGE producerFrame=%llu compared=%u contained=%u",
            static_cast<unsigned long long>(producerFrame),compared,contained);
        if(compared!=contained)log(
            "NATIVE_ADT_EXTENTS native=(%.4f %.4f %.4f)-(%.4f %.4f %.4f) submitted=(%.4f %.4f %.4f)-(%.4f %.4f %.4f)",
            firstMismatch.low[0],firstMismatch.low[1],firstMismatch.low[2],
            firstMismatch.high[0],firstMismatch.high[1],firstMismatch.high[2],
            submittedLow[0],submittedLow[1],submittedLow[2],submittedHigh[0],submittedHigh[1],submittedHigh[2]);
        compared=contained=0;
    }

    void record(const Evidence& native,const float* low,const float* high,Log log){
        if(producerFrame!=native.producerFrame){
            flush(log);
            producerFrame=native.producerFrame;
        }
        if(native.contains(low,high))++contained;
        else if(compared==contained){
            firstMismatch=native;
            for(unsigned axis=0;axis<3;++axis){submittedLow[axis]=low[axis];submittedHigh[axis]=high[axis];}
        }
        ++compared;
    }
};

inline thread_local const Evidence* current=nullptr;
struct Scope {
    const Evidence* before=current;
    explicit Scope(const Evidence* evidence){current=evidence;}
    ~Scope(){current=before;}
};
}
