#pragma once
#include <chrono>
#include <cstdint>

// Directional maps have first claim on the frame. Local shadows may carry a
// complete compatible cube for one due frame while animated casters move.
class NorthlightShadowRefreshBudget {
public:
    using Clock=std::chrono::steady_clock;
    NorthlightShadowRefreshBudget(unsigned targetMs,Clock::duration directional)
        : enabled_(targetMs!=0), deadline_(Clock::now()+std::chrono::milliseconds(targetMs)-directional) {}

    bool mayHold(bool heldLastTime,uint32_t ageMs,uint32_t refreshPeriodMs)const{
        // A slow frame must still publish a fresh pose. The normal crowd
        // cadence is 33 ms; allow at most one additional such interval.
        return enabled_&&!heldLastTime&&ageMs<refreshPeriodMs+33u&&Clock::now()>=deadline_;
    }
private:
    bool enabled_;
    Clock::time_point deadline_;
};
