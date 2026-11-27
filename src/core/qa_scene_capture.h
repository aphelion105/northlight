#pragma once
#include <atomic>
#include <cstdint>
#include <deque>

namespace NorthlightQaScene {
enum class State { Disabled, Recording, Requested, Saved };
inline std::atomic<State> state{State::Disabled};

inline void requestSave() noexcept{
    auto expected=State::Recording;
    state.compare_exchange_strong(expected,State::Requested);
}

class History {
    static constexpr std::uint64_t WindowMs=15000;
    struct Present { unsigned frame; std::uint64_t tick; };
    std::deque<Present> frames_;
public:
    void clear(){frames_.clear();}
    bool present(unsigned frame,std::uint64_t tick,bool save,unsigned& first,std::uint64_t& duration){
        // Keep the frame that was on screen fifteen seconds before the mark.
        while(frames_.size()>1&&tick-frames_[1].tick>=WindowMs)frames_.pop_front();
        frames_.push_back({frame,tick});
        if(!save)return false;
        first=frames_.front().frame;
        duration=tick-frames_.front().tick;
        return true;
    }
};
}
