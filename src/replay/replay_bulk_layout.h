#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

// Render-owned bounded scratch. Only immutable snapshot POINTER identity is
// shared, never hashes of geometry/poses. Ownership is held by this frame's
// replays. No bindings or decisions survive a layout restart or another frame.
namespace NorthlightReplayBulk {
class Layout {
    static constexpr std::size_t Slots=8192;
    struct Slot {const void* key=nullptr;std::uint32_t epoch=0;std::uint16_t owner=0;};
    std::array<Slot,Slots> slots_{};
    std::array<std::uint16_t,4096> owners_{};
    std::uint32_t epoch_=0;
    const void* lastKey_=nullptr;std::uint16_t lastOwner_=0;
public:
    static constexpr std::size_t Limit=4096;
    void begin(){
        lastKey_=nullptr;
        if(++epoch_==0){for(auto& slot:slots_)slot.epoch=0;epoch_=1;}
    }
    std::size_t add(const void* key,std::size_t draw){
        if(draw>=Limit)return draw;
        auto& owner=owners_[draw];owner=std::uint16_t(draw);
        if(!key)return draw; // Owned/mutable snapshots always get unique storage.
        if(key==lastKey_){owner=lastOwner_;return owner;}
        std::size_t at=((reinterpret_cast<std::uintptr_t>(key)>>4)*std::uintptr_t(2654435761u))&(Slots-1);
        for(std::size_t visited=0;visited<Slots;++visited,at=(at+1)&(Slots-1)){
            auto& slot=slots_[at];
            if(slot.epoch!=epoch_){slot={key,epoch_,owner};lastKey_=key;lastOwner_=owner;return draw;}
            if(slot.key==key){owner=slot.owner;lastKey_=key;lastOwner_=owner;return owner;}
        }
        return draw; // Fail open to the original per-draw copy.
    }
    bool unique(std::size_t draw)const{return draw>=Limit||owners_[draw]==draw;}
};

// One successful bulk layout publishes the nonresident packet order for its
// upload and bindings. Starts over when admission or pressure changes it.
class DrawList {
    std::array<std::uint16_t,Layout::Limit> draws_{};
    std::size_t count_=0;
    bool indexed_=true;
public:
    void clear(){count_=0;indexed_=true;}
    void add(std::size_t i){
        if(i>=draws_.size())indexed_=false;
        if(indexed_)draws_[count_]=static_cast<std::uint16_t>(i);
        ++count_;
    }
    std::size_t size()const{return count_;}
    bool indexed()const{return indexed_;}
    template<class Replays,class Visit>
    void visit(const Replays& replays,Visit&& visitDraw)const{
        if(indexed_){
            for(std::size_t i=0;i<count_;++i)visitDraw(draws_[i]);
        }else{
            for(std::size_t i=0;i<replays.size();++i)if(!replays[i]->gpuCached)visitDraw(i);
        }
    }
};
}
