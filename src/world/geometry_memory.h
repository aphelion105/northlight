#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>

// Admission estimates, not a guarantee against allocations by the game/driver.
// The owner queries process address space immediately before each costly stage.
namespace NorthlightGeometryMemory {
constexpr uint64_t MiB=1024ull*1024ull;
constexpr uint64_t ProcessReserve=512*MiB;
// Contiguous headroom above the largest single allocation of a stage. A 32-bit
// process whose largest free region shrank to ~20 MiB still reported 2.5 GiB
// free before one silent exit; every large allocation now needs this margin.
constexpr uint64_t ContiguousMargin=32*MiB;
constexpr uint64_t ChunkMargin=16*MiB;
// Aggregate headroom kept for allocations INSIDE a build that already passed buildBudget at stage entry.
// Entry reserved the build's own headroom (256 MiB); re-requiring it per allocation double-counts the
// memory the build itself consumes. Interior checks keep the process reserve plus this guard.
constexpr uint64_t InBuildHeadroom=128*MiB;
struct Sample { uint64_t available=0,largest=0;bool valid=false; };
struct Budget { uint64_t available=0,largest=0;bool valid=false; };
inline bool add(uint64_t a,uint64_t b,uint64_t& out){
    if(b>std::numeric_limits<uint64_t>::max()-a)return false;
    out=a+b;return true;
}
inline Budget buildBudget(uint64_t largestAllocation=0){
    // Aggregate headroom plus a contiguous margin for the stage's largest block.
    Budget b;if(!add(largestAllocation,ContiguousMargin,b.largest))return {};
    b.available=ProcessReserve+256*MiB;b.valid=true;return b;
}
inline Budget inBuildBudget(uint64_t largestAllocation=0){
    // Same contiguous rule as buildBudget; only the aggregate headroom is smaller (see InBuildHeadroom).
    Budget b;if(!add(largestAllocation,ContiguousMargin,b.largest))return {};
    b.available=ProcessReserve+InBuildHeadroom;b.valid=true;return b;
}
inline uint64_t roundUp(uint64_t bytes,uint64_t step){
    if(!step)return bytes;uint64_t rounded=0;
    if(!add(bytes,step-1,rounded))return bytes;return rounded-rounded%step;
}
// Growth of a DYNAMIC buffer: the driver keeps two or three renamed slices of
// the new capacity alive, and the new backing must fit one contiguous block.
inline Budget dynamicBudget(uint64_t capacityBytes){
    Budget b;uint64_t twice=0;
    if(!add(capacityBytes,capacityBytes,twice)||!add(twice,ChunkMargin,b.largest)||
       !add(twice,capacityBytes,b.available)||!add(b.available,ProcessReserve,b.available))return {};
    b.valid=true;return b;
}
constexpr uint64_t SmallPageCapacityLimit=512ull*1024ull;
// After the complete generation passed full upload admission, each bounded
// MANAGED page may use a fresh GlobalMemoryStatusEx available-VA sample instead
// of another full VirtualQuery sweep. This checks aggregate reserve only: it
// does not assert any contiguous block exists. Actual Create*Buffer failures
// must retain the old complete generation and enter the normal retry path.
// Reserve three capacities as for other growth, and debit successes from the
// shared sample before admitting another allocation in the same frame.
inline bool admitsSmallPageGrowth(bool valid,uint64_t available,uint64_t capacityBytes){
    if(!valid||!capacityBytes||capacityBytes>SmallPageCapacityLimit)return false;
    uint64_t twice=0,reserved=0,required=0;
    return add(capacityBytes,capacityBytes,twice)&&add(twice,capacityBytes,reserved)&&
           add(ProcessReserve,reserved,required)&&available>=required;
}
// A fresh sample can cover the consecutive local VB/IB allocation transaction.
// Reserve the same three-capacity allowance used by dynamicBudget after each
// successful growth; never credit a released old buffer before re-sampling.
// This is a conservative reservation estimate, not a guarantee against other
// threads or driver fragmentation. Keep the existing admission margins/checks.
inline void debitGrowth(Sample& sample,uint64_t capacityBytes){
    uint64_t twice=0,reserved=0;
    if(!sample.valid||!add(capacityBytes,capacityBytes,twice)||!add(twice,capacityBytes,reserved)){
        sample={};return;
    }
    sample.available=reserved>=sample.available?0:sample.available-reserved;
    sample.largest=reserved>=sample.largest?0:sample.largest-reserved;
}
inline Budget uploadBudget(uint64_t vertexBytes,uint64_t indexBytes,
                           uint64_t textureBytes,uint64_t largestTextureBytes){
    Budget b;uint64_t bytes=0,doubled=0;
    if(largestTextureBytes>textureBytes||!add(vertexBytes,indexBytes,bytes)||
       !add(bytes,textureBytes,bytes)||!add(bytes,bytes,doubled)||
       !add(ProcessReserve+64*MiB,doubled,b.available))return {};
    uint64_t largest=vertexBytes>indexBytes?vertexBytes:indexBytes;
    if(largestTextureBytes>largest)largest=largestTextureBytes;
    // Twice the largest resource plus a chunk margin: driver sub-allocation
    // chunks and the managed host copy both need contiguous space.
    if(!add(largest,largest,b.largest)||!add(b.largest,ChunkMargin,b.largest))return {};
    b.valid=true;return b;
}
inline bool admits(Sample sample,Budget budget){
    return sample.valid&&budget.valid&&sample.available>=budget.available&&
           sample.largest>=budget.largest;
}

// Worker-owned only. Weak ownership counts real retained generations without
// prolonging them; aliases sharing a control block count as one generation.
template<class T,size_t Limit=2> class Generations {
    static_assert(Limit>0,"generation limit must be positive");
    std::array<std::weak_ptr<T>,Limit> entries_;
public:
    size_t live(){size_t count=0;for(auto& e:entries_){if(e.expired())e.reset();else ++count;}return count;}
    bool canAdmit(){return live()<Limit;}
    bool track(const std::shared_ptr<T>& value){
        if(!value)return false;
        live();
        for(const auto& e:entries_)if(!e.expired()&&!e.owner_before(value)&&!value.owner_before(e))return true;
        for(auto& e:entries_)if(e.expired()){e=value;return true;}
        return false;
    }
};
// 0.3.156 watchdog (log only): one generation-admission stall episode. The builder
// marks each deferral and clears on admission; the render thread reports once per
// episode that outlives `ms`. The owner guards it with its own mutex.
struct StallWatch {
    // 0.3.157: an explicit armed flag (since=now|1 could lie one tick in the future: now-since wrapped to
    // 0xFFFFFFFF and the watchdog fired at once with ms=4294967295).
    uint32_t since=0;bool armed=false,reported=false;
    void deferred(uint32_t now){if(!armed){armed=true;since=now;}}
    void admitted(){armed=false;reported=false;}
    bool due(uint32_t now,uint32_t ms){if(!armed||reported||uint32_t(now-since)<ms)return false;reported=true;return true;}
};
} // namespace NorthlightGeometryMemory
