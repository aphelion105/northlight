#pragma once
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <mutex>

// Presentation intervals, not GPU busy time. No device calls or allocations in
// publish(). Collection is leased by the visible monitor/HUD and otherwise off.
namespace NorthlightPerformance {
inline uint64_t nowMs(){return uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());}
struct Frame {
    double effectsCpuMs=-1,effectsGpuMs=-1,eventWaitMs=-1,queueIdleMs=-1;
    uint64_t eventWaitNs=0,queueIdleNs=0;bool streamed=false;
    unsigned sceneWidth=0,sceneHeight=0;uint64_t at=0;
};
struct Timing {
    static constexpr unsigned GraphSize=240;
    unsigned samples=0,graphCount=0;double fps=0,meanMs=0,maxMs=0,low1=-1,low01=-1;
    std::array<float,GraphSize> graph{};Frame frame;
};
class History {
    static constexpr unsigned Capacity=4096;
    std::array<float,Capacity> samples_{};unsigned count_=0,next_=0;
    int64_t previous_=0;bool primed_=false,countersKnown_=false;
    uint64_t event_=0,idle_=0;Frame latest_;
public:
    void reset(){count_=next_=0;discontinuity();latest_={};}
    void discontinuity(){primed_=countersKnown_=false;}
    void add(int64_t tick,int64_t frequency,bool eligible,Frame frame){
        if(!eligible||frequency<=0){discontinuity();return;}
        if(frame.streamed&&countersKnown_){
            frame.eventWaitMs=frame.eventWaitNs>=event_?double(frame.eventWaitNs-event_)*1e-6:-1;
            frame.queueIdleMs=frame.queueIdleNs>=idle_?double(frame.queueIdleNs-idle_)*1e-6:-1;
        }
        countersKnown_=frame.streamed;event_=frame.eventWaitNs;idle_=frame.queueIdleNs;latest_=frame;
        if(primed_){const double ms=double(tick-previous_)*1000/double(frequency);
            if(ms>0&&ms<=2000&&std::isfinite(ms)){samples_[next_]=float(ms);next_=(next_+1)%Capacity;count_=std::min(count_+1,Capacity);}}
        previous_=tick;primed_=true;
    }
    void graph(Timing& out)const{
        out.frame=latest_;out.samples=count_;out.graphCount=std::min(count_,Timing::GraphSize);
        for(unsigned i=0;i<out.graphCount;++i)out.graph[i]=samples_[(next_+Capacity-out.graphCount+i)%Capacity];
    }
    Timing read()const{
        Timing out;graph(out);if(!count_)return out;
        std::array<float,Capacity> ordered{};double sum=0;
        for(unsigned i=0;i<count_;++i){ordered[i]=samples_[i];sum+=ordered[i];}
        std::sort(ordered.begin(),ordered.begin()+count_);out.meanMs=sum/count_;out.fps=1000/out.meanMs;out.maxMs=ordered[count_-1];
        const auto low=[&](unsigned divisor){const unsigned n=(count_+divisor-1)/divisor;double slow=0;for(unsigned i=count_-n;i<count_;++i)slow+=ordered[i];return 1000*n/slow;};
        if(count_>=100)out.low1=low(100);if(count_>=1000)out.low01=low(1000);
        return out;
    }
};
class Store {
    std::mutex mutex_;History history_;uintptr_t owner_=0;
    std::atomic<uintptr_t> requestedOwner_{0};std::atomic<uint64_t> until_{0};
    std::atomic<bool> skipped_{false};
public:
    void touch(uintptr_t owner){requestedOwner_.store(owner,std::memory_order_relaxed);until_.store(nowMs()+2000,std::memory_order_release);}
    bool active()const{const auto deadline=until_.load(std::memory_order_acquire);return deadline&&nowMs()<=deadline;}
    bool wanted(uintptr_t owner)const{return active()&&requestedOwner_.load(std::memory_order_relaxed)==owner;}
    bool expire(){auto deadline=until_.load(std::memory_order_acquire);if(deadline&&nowMs()>deadline)until_.compare_exchange_strong(deadline,0,std::memory_order_acq_rel);return active();}
    void detach(uintptr_t owner){if(requestedOwner_.load(std::memory_order_relaxed)==owner)until_.store(0,std::memory_order_release);}
    void publish(uintptr_t owner,int64_t tick,int64_t frequency,bool eligible,const Frame& frame){
        if(!wanted(owner))return;std::unique_lock<std::mutex> lock(mutex_,std::try_to_lock);
        if(!lock){skipped_.store(true,std::memory_order_relaxed);return;}
        if(owner_!=owner){owner_=owner;history_.reset();}
        if(skipped_.exchange(false,std::memory_order_relaxed))history_.discontinuity();history_.add(tick,frequency,eligible,frame);
    }
    bool read(uintptr_t owner,Timing& out){
        History copy;{std::unique_lock<std::mutex> lock(mutex_,std::try_to_lock);if(!lock||owner_!=owner)return false;copy=history_;}
        out=copy.read();return true;
    }
    bool graph(uintptr_t owner,Timing& out){std::unique_lock<std::mutex> lock(mutex_,std::try_to_lock);if(!lock||owner_!=owner)return false;history_.graph(out);return true;}
    void reset(uintptr_t owner){std::unique_lock<std::mutex> lock(mutex_,std::try_to_lock);if(lock&&owner_==owner)history_.reset();}
};
inline Store& store(){static Store value;return value;}
}
